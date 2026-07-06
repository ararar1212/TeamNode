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

// Fast continuous beeping. Lower this number for an even more frantic beep.
static const unsigned long BEEP_TOGGLE_MS = 60;

static bool alarmActive =
    false; // desired alarm state (what we report to the dashboard)
static bool sirenRunning =
    false; // whether the beep state machine is currently driving the pin
static bool buzzerPinState = false;
static unsigned long sirenLastToggle = 0;

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

// Call this every loop() iteration (not just once a second) so the pulsing is
// smooth.
static void updateSiren(bool active, unsigned long now) {
  alarmActive = active;

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
  delay(500);

  Wire.begin();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

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

  const unsigned long now = millis();

  // ===== NEW: Detect falls every loop iteration =====
  bool fallJustDetected = detectFall(accelX, accelY, accelZ);
  packet.fall_detected = isFalling() ? 1 : 0;

  // Drive the siren pattern every loop iteration for smooth pulsing, using the
  // most recently computed risk state.
  updateSiren(alarmActive, now);

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
    
    // ===== NEW: Trigger alarm if fall detected OR risk is high =====
    alarmActive = isRedLevel || fallJustDetected;
    
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
    // ===== NEW: Display fall status =====
    Serial.printf("Fall        : %s\n", packet.fall_detected ? "YES ⚠️" : "no");
    Serial.printf("Risk        : score=%d level=%s buzzer=%s\n",
                  packet.risk_score, isRedLevel ? "RED" : "ok",
                  packet.buzzer_active ? "ON" : "off");
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
      // ===== NEW: Add fall status to OLED display =====
      display.println("-- FALL --");
      display.print(packet.fall_detected ? "FALL!" : "OK");
    }

    display.display();
  }
}
