#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <math.h>
#include <string.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "BMP180Sensor.h"
#include "DHT11Sensor.h"
#include "MAX30102Sensor.h"
#include "MPU6050Sensor.h"
#include "MQ135Sensor.h"
#include "MQ5Sensor.h"
#include "TelemetryPacket.h"
#include <TinyGPSPlus.h>

static TinyGPSPlus gps;
static HardwareSerial SerialGPS(2);

static Adafruit_SSD1306 display(128, 64, &Wire, -1);

// Broadcast by default so the sender can talk to any listening ESP-NOW
// receiver.
static uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static esp_now_peer_info_t peerInfo;

static TelemetryPacket packet;

static unsigned long lastUpdate = 0;
static const unsigned long UPDATE_INTERVAL_MS = 1000;
static unsigned long lastPageChange = 0;
static const unsigned long PAGE_INTERVAL_MS = 1200;
static uint8_t page = 0;
static const uint8_t PAGE_COUNT = 4;
static uint32_t sequenceNumber = 0;

static float lastValidTemp = 0.0f;
static float lastValidHumidity = 0.0f;
static float lastValidPressure = 0.0f;
static float lastValidAltitude = 0.0f;

static const float SEA_LEVEL_PRESSURE_PA = 101325.0f;

// ---- Buzzer config (active buzzer, on/off only) ----
// Change this pin to match your wiring.
static const uint8_t BUZZER_PIN = 25;

// ---- SOS Button Config ----
static const uint8_t SOS_BTN_PIN = 12;
static unsigned long sosStartTime = 0;
static bool sosActive = false;

// ---- Gas Alarm Config ----
static unsigned long gasAlarmStartTime = 0;
static bool gasAlarmActive = false;

// Fast continuous beeping. Lower this number for an even more frantic beep.
static const unsigned long BEEP_TOGGLE_MS = 60;

// Beep-beep pattern (slower than continuous alarm)
// Pattern: beep 300ms, silence 200ms, beep 300ms, silence 1000ms (then repeat)
static const unsigned long BEEP_BEEP_ON_MS = 300;
static const unsigned long BEEP_BEEP_OFF_MS = 200;
static const unsigned long BEEP_BEEP_LONG_SILENCE_MS = 1000;

static bool alarmActive =
    false; // desired alarm state (what we report to the dashboard)
static bool sirenRunning =
    false; // whether the beep state machine is currently driving the pin
static bool buzzerPinState = false;
static unsigned long sirenLastToggle = 0;

// ===== NEW: Immobility detection state =====
// Detects if worker fell but is not moving (possible injury)
static bool fallWithImmobility = false;
static unsigned long immobilityStartTime = 0;
static const int IMMOBILITY_MOTION_THRESHOLD =
    10; // Motion must stay below this
static const unsigned long IMMOBILITY_DETECT_MS =
    2000; // 2 seconds of low motion after fall

// ---- Risk thresholds (mirrors riskScore.js) ----
static const int MQ135_WARNING = 1600;
static const int MQ135_DANGER = 2200;
static const int MQ5_WARNING = 1200;
static const int MQ5_DANGER = 1800;

static float computeAltitudeMeters(float pressurePa) {
  // Standard barometric formula (assumes sea-level reference pressure).
  return 44330.0f * (1.0f - powf(pressurePa / SEA_LEVEL_PRESSURE_PA, 0.1903f));
}

static int computeMotionLevel(float ax, float ay, float az) {
  const float accelMagnitudeG = sqrtf(ax * ax + ay * ay + az * az) / 9.81f;
  const float deviationFromRest = fabsf(accelMagnitudeG - 1.0f);
  int motion = (int)(deviationFromRest * 120.0f);
  if (motion < 0) {
    motion = 0;
  }
  if (motion > 100) {
    motion = 100;
  }
  return motion;
}

// Simplified port of computeRisk() from riskScore.js.
// Returns the 0-100 score; sets isRedLevel to true if the helmet should alarm.
static int computeRiskScore(int pulse, float ambientTemp, float humidity,
                            int motion, int mq135, int mq5, bool &isRedLevel) {
  int score = 0;
  bool gasDanger = false;

  if (pulse >= 160)
    score += 40;
  else if (pulse >= 140)
    score += 25;
  else if (pulse >= 120)
    score += 12;

  const float heatIndex = ambientTemp + 0.05f * humidity;
  if (heatIndex >= 40.0f)
    score += 20;
  else if (heatIndex >= 35.0f)
    score += 10;

  if (motion < 20 && pulse >= 120)
    score += 10;

  if (mq135 >= MQ135_DANGER) {
    score += 45;
    gasDanger = true;
  } else if (mq135 >= MQ135_WARNING) {
    score += 18;
  }

  if (mq5 >= MQ5_DANGER) {
    score += 45;
    gasDanger = true;
  } else if (mq5 >= MQ5_WARNING) {
    score += 18;
  }

  if (score > 100)
    score = 100;

  // Same rule as the dashboard: score >= 60 is red, and gas danger always
  // forces red.
  isRedLevel = (score >= 60) || gasDanger;

  return score;
}

// ===== NEW: Detect immobility after a fall =====
// If worker falls but doesn't move for 2+ seconds, it indicates possible injury
static void updateImmobilityDetection(bool isFallen, int motion,
                                      unsigned long now) {
  if (!isFallen) {
    // No fall, reset immobility state
    fallWithImmobility = false;
    immobilityStartTime = 0;
    return;
  }

  // Worker fell. Check if they're immobile (not moving).
  if (motion < IMMOBILITY_MOTION_THRESHOLD) {
    if (!fallWithImmobility) {
      // Just started being immobile after fall
      immobilityStartTime = now;
    } else if (now - immobilityStartTime >= IMMOBILITY_DETECT_MS) {
      // Confirmed: immobile for 2+ seconds after fall → injury likely
      fallWithImmobility = true;
      return;
    }
  } else {
    // Worker is moving again, clear immobility detection
    fallWithImmobility = false;
    immobilityStartTime = 0;
  }
}

// ===== NEW: Update buzzer pattern based on immobility =====
// - Continuous beeping if SOS active
// - Continuous beeping if just fell (fast beep)
// - Beep-beep pattern (spaced) if fallen AND immobile (possible injury)
// - Beep-beep pattern if gas alarm active
static void updateBuzzerPattern(bool isFallen, bool isImmobile, bool isSos, bool isGasAlarm,
                                unsigned long now) {
  // ===== CASE 1: SOS Active =====
  if (isSos) {
    alarmActive = true;
    if (!sirenRunning) {
      sirenRunning = true;
      sirenLastToggle = now;
      buzzerPinState = true;
      digitalWrite(BUZZER_PIN, HIGH);
      return;
    }
    if (now - sirenLastToggle >= BEEP_TOGGLE_MS) {
      sirenLastToggle = now;
      buzzerPinState = !buzzerPinState;
      digitalWrite(BUZZER_PIN, buzzerPinState ? HIGH : LOW);
    }
    return;
  }

  // ===== CASE 2: Gas Alarm =====
  if (isGasAlarm && !isFallen) {
    alarmActive = true;
    if (!sirenRunning) {
      sirenRunning = true;
      sirenLastToggle = now;
      buzzerPinState = true;
      digitalWrite(BUZZER_PIN, HIGH);
      return;
    }
    // Fast beep-beep pattern for gas
    static unsigned long gasCycleStart = 0;
    if (gasCycleStart == 0) gasCycleStart = now;
    unsigned long elapsed = now - gasCycleStart;
    if (elapsed >= 1000) { gasCycleStart = now; elapsed = 0; }
    
    bool shouldBeOn = (elapsed < 200) || (elapsed >= 300 && elapsed < 500);
    if (shouldBeOn != buzzerPinState) {
      buzzerPinState = shouldBeOn;
      digitalWrite(BUZZER_PIN, buzzerPinState ? HIGH : LOW);
    }
    return;
  }

  if (!isFallen) {
    // No fall, buzzer off
    alarmActive = false;
    if (sirenRunning) {
      sirenRunning = false;
      buzzerPinState = false;
      digitalWrite(BUZZER_PIN, LOW);
    }
    return;
  }

  // ===== CASE 1: Fall detected but worker is moving (recovering) =====
  // Use fast continuous beeping
  if (!isImmobile) {
    alarmActive = true;

    if (!sirenRunning) {
      sirenRunning = true;
      sirenLastToggle = now;
      buzzerPinState = true;
      digitalWrite(BUZZER_PIN, HIGH);
      return;
    }

    // Fast toggle for continuous beeping
    if (now - sirenLastToggle >= BEEP_TOGGLE_MS) {
      sirenLastToggle = now;
      buzzerPinState = !buzzerPinState;
      digitalWrite(BUZZER_PIN, buzzerPinState ? HIGH : LOW);
    }
    return;
  }

  // ===== CASE 2: Fall detected AND immobile (possible injury) =====
  // Use slow beep-beep pattern (beep-beep-[long silence]-repeat)
  alarmActive = true;

  if (!sirenRunning) {
    sirenRunning = true;
    sirenLastToggle = now;
    buzzerPinState = true;
    digitalWrite(BUZZER_PIN, HIGH);
    return;
  }

  // Beep-beep pattern state machine
  static unsigned long patternCycleStart = 0;
  static int patternPhase = 0; // 0=beep1, 1=silence1, 2=beep2, 3=long_silence

  if (patternCycleStart == 0) {
    patternCycleStart = now;
    patternPhase = 0;
  }

  unsigned long elapsedInCycle = now - patternCycleStart;

  // Calculate phase based on elapsed time
  unsigned long phase1End = BEEP_BEEP_ON_MS;
  unsigned long phase2End = phase1End + BEEP_BEEP_OFF_MS;
  unsigned long phase3End = phase2End + BEEP_BEEP_ON_MS;
  unsigned long cycleEnd = phase3End + BEEP_BEEP_LONG_SILENCE_MS;

  if (elapsedInCycle >= cycleEnd) {
    // Cycle complete, restart
    patternCycleStart = now;
    elapsedInCycle = 0;
  }

  // Determine if buzzer should be ON or OFF
  bool shouldBeOn = false;
  if (elapsedInCycle < phase1End) {
    shouldBeOn = true; // First beep
  } else if (elapsedInCycle < phase2End) {
    shouldBeOn = false; // Silence
  } else if (elapsedInCycle < phase3End) {
    shouldBeOn = true; // Second beep
  } else {
    shouldBeOn = false; // Long silence before repeat
  }

  if (shouldBeOn != buzzerPinState) {
    buzzerPinState = shouldBeOn;
    digitalWrite(BUZZER_PIN, buzzerPinState ? HIGH : LOW);
  }
}

// Call this every loop() iteration (not just once a second) so the pulsing is
// smooth.
static void updateSiren(bool active, unsigned long now) {
  // This function is now replaced by updateBuzzerPattern() for fall detection
  // Kept for reference but not called in main loop anymore

  if (!active) {
    if (sirenRunning) {
      sirenRunning = false;
      buzzerPinState = false;
      digitalWrite(BUZZER_PIN, LOW);
    }
    return;
  }

  if (!sirenRunning) {
    sirenRunning = true;
    sirenLastToggle = now;
    buzzerPinState = true;
    digitalWrite(BUZZER_PIN, HIGH);
    return;
  }

  if (now - sirenLastToggle >= BEEP_TOGGLE_MS) {
    sirenLastToggle = now;
    buzzerPinState = !buzzerPinState;
    digitalWrite(BUZZER_PIN, buzzerPinState ? HIGH : LOW);
  }
}

static void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  (void)mac;
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("ESP-NOW send failed");
  }
}

void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600);
  delay(500);

  Wire.begin();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(SOS_BTN_PIN, INPUT);

  dht11Init();
  mq135Init();
  mq5Init();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
  }

  if (!bmp180Init()) {
    Serial.println("BMP180 not found");
  }
  // mpu6050Init() now also calls fallDetectorInit() internally
  if (!mpu6050Init()) {
    Serial.println("MPU6050 not found");
  }
  if (!max30102Init()) {
    Serial.println("MAX30102 not found");
  }

  memset(&packet, 0, sizeof(packet));
  strncpy(packet.worker_id, "w1", sizeof(packet.worker_id) - 1);
  strncpy(packet.worker_name, "Helmet 1", sizeof(packet.worker_name) - 1);

  WiFi.mode(WIFI_STA);
  Serial.print("Sender MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
  }
}

void loop() {
  packet.ir_value = max30102ReadIR();
  packet.bpm = max30102UpdateBpm(packet.ir_value);
  float accelX = 0.0f;
  float accelY = 0.0f;
  float accelZ = 0.0f;
  mpu6050Read(accelX, accelY, accelZ);
  packet.accel_x = accelX;
  packet.accel_y = accelY;
  packet.accel_z = accelZ;
  packet.motion =
      computeMotionLevel(packet.accel_x, packet.accel_y, packet.accel_z);

  while (SerialGPS.available() > 0) {
    gps.encode(SerialGPS.read());
  }

  if (gps.location.isValid()) {
    packet.gps_lat = gps.location.lat();
    packet.gps_lng = gps.location.lng();
  } else {
    packet.gps_lat = 0.0f;
    packet.gps_lng = 0.0f;
  }

  const unsigned long now = millis();

  // ===== NEW: Detect falls every loop iteration =====
  bool fallJustDetected = detectFall(accelX, accelY, accelZ);
  packet.fall_detected = isFalling() ? 1 : 0;

  // ===== NEW: Detect SOS button press =====
  if (digitalRead(SOS_BTN_PIN) == HIGH) {
    if (!sosActive) {
      sosActive = true;
      sosStartTime = now;
    }
  }

  // Auto-clear SOS after 5 seconds of buzzer
  if (sosActive && (now - sosStartTime >= 5000)) {
    sosActive = false;
  }
  packet.sos_alert = sosActive ? 1 : 0;

  // ===== NEW: Detect immobility after fall =====
  updateImmobilityDetection(packet.fall_detected, packet.motion, now);

  // Auto-clear Gas Alarm after 5 seconds
  if (gasAlarmActive && (now - gasAlarmStartTime >= 5000)) {
    gasAlarmActive = false;
  }

  // ===== NEW: Update buzzer pattern based on fall, SOS, and Gas state =====
  updateBuzzerPattern(packet.fall_detected, fallWithImmobility, sosActive, gasAlarmActive, now);

  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;

    float temp = NAN;
    float humidity = NAN;
    dht11Read(temp, humidity);

    if (!isnan(temp)) {
      lastValidTemp = temp;
    }
    if (!isnan(humidity)) {
      lastValidHumidity = humidity;
    }
    packet.temperature = lastValidTemp;
    packet.humidity = lastValidHumidity;

    packet.mq135_val = mq135Read();
    packet.mq5_val = mq5Read();

    const float pressure = bmp180ReadPressure();
    if (!isnan(pressure) && pressure > 1000.0f) {
      lastValidPressure = pressure;
      lastValidAltitude = computeAltitudeMeters(pressure);
    }
    packet.altitude = lastValidAltitude;

    packet.sequence = ++sequenceNumber;
    packet.uptime_ms = now;

    // ---- Local risk check ----
    // This just decides whether the alarm SHOULD be on; updateSiren() (called
    // every loop iteration above) is what actually pulses the pin.
    bool isRedLevel = false;
    const int riskScore = computeRiskScore(
        packet.bpm, packet.temperature, packet.humidity, packet.motion,
        packet.mq135_val, packet.mq5_val, isRedLevel);

    // Check for gas anomalies specifically
    bool gasAnomaly = (packet.mq135_val >= MQ135_WARNING || packet.mq5_val >= MQ5_WARNING);
    if (gasAnomaly) {
      if (!gasAlarmActive) {
        gasAlarmActive = true;
        gasAlarmStartTime = now;
      }
    }

    // ===== UPDATED: Don't override fall-based or SOS-based alarm logic =====
    // Fall detection, SOS, and gas buzzer are now managed by updateBuzzerPattern()
    if (!packet.fall_detected && !packet.sos_alert && !gasAlarmActive) {
      alarmActive = isRedLevel;
    }

    packet.risk_score = riskScore;
    packet.buzzer_active = alarmActive ? 1 : 0;

    const esp_err_t sendStatus =
        esp_now_send(receiverAddress, (uint8_t *)&packet, sizeof(packet));
    if (sendStatus != ESP_OK) {
      Serial.printf("ESP-NOW queue error: %d\n", (int)sendStatus);
    }

    Serial.println("\n========== HELMET SENSOR DATA ==========");
    Serial.printf("Worker      : %s (%s)\n", packet.worker_name,
                  packet.worker_id);
    Serial.printf("Environment : Temp: %.1f C | Hum: %.1f %% | Alt: %.1f m\n",
                  packet.temperature, packet.humidity, packet.altitude);
    Serial.printf("Air Quality : MQ-135: %d | MQ-5: %d\n", packet.mq135_val,
                  packet.mq5_val);
    Serial.printf("Vitals      : BPM: %d | IR: %ld\n", packet.bpm,
                  packet.ir_value);
    Serial.printf("Motion (m/s2): X: %.2f | Y: %.2f | Z: %.2f | Motion: %d\n",
                  packet.accel_x, packet.accel_y, packet.accel_z,
                  packet.motion);
    // ===== NEW: Display fall, SOS, and immobility status =====
    Serial.printf("Fall/SOS    : Fall=%s | Immobile=%s | SOS=%s\n",
                  packet.fall_detected ? "YES ⚠️" : "no",
                  fallWithImmobility ? "YES (INJURY RISK)" : "no",
                  packet.sos_alert ? "ACTIVE 🚨" : "no");
    Serial.printf("Risk        : score=%d level=%s buzzer=%s\n",
                  packet.risk_score, isRedLevel ? "RED" : "ok",
                  packet.buzzer_active ? "ON" : "off");
    Serial.printf("GPS         : Lat=%.6f | Lng=%.6f\n", packet.gps_lat,
                  packet.gps_lng);
    Serial.printf("Packet      : seq=%lu uptime=%lu ms\n",
                  (unsigned long)packet.sequence,
                  (unsigned long)packet.uptime_ms);
    Serial.println("========================================");
  }

  if (now - lastPageChange >= PAGE_INTERVAL_MS) {
    lastPageChange = now;
    page = (page + 1) % PAGE_COUNT;

    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);

    if (page == 0) {
      display.println("-- ENV -- ");
      display.print("Temp:");
      display.print(packet.temperature, 1);
      display.println("C");
      display.print("Hum:");
      display.print(packet.humidity, 1);
      display.println("%");
    } else if (page == 1) {
      display.println("-- ALT --");
      display.print(packet.altitude, 1);
      display.println("m");
    } else if (page == 2) {
      display.println("-- AQI --");
      display.print("MQ135:");
      display.println(packet.mq135_val);
      display.print("MQ5:");
      display.println(packet.mq5_val);
    } else if (page == 3) {
      display.println("-- BPM --");
      display.print(packet.bpm);
      display.println(" bpm");
    } else {
      // ===== NEW: Display fall and immobility status on OLED =====
      display.println("-- FALL --");
      if (packet.fall_detected) {
        if (fallWithImmobility) {
          display.println("FALL!");
          display.println("IMMOBILE!");
        } else {
          display.println("FALL");
          display.println("MOVING OK");
        }
      } else {
        display.println("OK");
      }
    }

    display.display();
  }
}
