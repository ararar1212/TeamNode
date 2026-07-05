#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "DHT11Sensor.h"
#include "MQ135Sensor.h"
#include "MQ5Sensor.h"
#include "BMP180Sensor.h"
#include "MPU6050Sensor.h"
#include "MAX30102Sensor.h"

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// --- ESP-NOW Configuration ---
// REPLACE WITH YOUR GATEWAY RECEIVER'S MAC ADDRESS
uint8_t gatewayAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
  float temperature;
  float humidity;
  int mq135_val;
  int mq5_val;
  float pressure;
  float accel_x;
  float accel_y;
  float accel_z;
  long ir_value;
  int bpm;
} struct_message;

struct_message sensorData;
esp_now_peer_info_t peerInfo;

// --- Non-blocking timing state ---
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 1000;   // slow sensors + ESP-NOW send + serial log, ms
unsigned long lastPageChange = 0;
const unsigned long PAGE_INTERVAL = 1000;     // OLED page rotation, ms
uint8_t page = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(); // Uses default GPIO 21 (SDA) and GPIO 22 (SCL)

  // Initialize Sensors
  dht11Init();
  mq135Init();
  mq5Init();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (!bmp180Init()) {
    Serial.println("BMP180 not found!");
  }

  if (!mpu6050Init()) {
    Serial.println("MPU6050 not found!");
  }

  if (!max30102Init()) {
    Serial.println("MAX30102 not found!");
  }

  // Initialize ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  memcpy(peerInfo.peer_addr, gatewayAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  // 1. Fast sensors -- every iteration, no delay
  sensorData.ir_value = max30102ReadIR();
  sensorData.bpm = max30102UpdateBpm(sensorData.ir_value);
  mpu6050Read(sensorData.accel_x, sensorData.accel_y, sensorData.accel_z);

  unsigned long now = millis();

  // 2. Slow sensors + ESP-NOW send + serial log -- once a second
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;

    dht11Read(sensorData.temperature, sensorData.humidity);
    sensorData.mq135_val = mq135Read();
    sensorData.mq5_val = mq5Read();
    sensorData.pressure = bmp180ReadPressure();

    esp_now_send(gatewayAddress, (uint8_t *) &sensorData, sizeof(sensorData));

    Serial.println("\n========== HELMET SENSOR DATA ==========");
    Serial.printf("Environment : Temp: %.1f C | Hum: %.1f %% | Press: %.0f Pa\n", sensorData.temperature, sensorData.humidity, sensorData.pressure);
    Serial.printf("Air Quality : MQ-135: %d | MQ-5: %d\n", sensorData.mq135_val, sensorData.mq5_val);
    Serial.printf("Vitals      : BPM: %d\n", sensorData.bpm);
    Serial.printf("IR raw data : %ld\n", sensorData.ir_value);
    Serial.printf("Motion (G)  : X: %.2f | Y: %.2f | Z: %.2f\n", sensorData.accel_x, sensorData.accel_y, sensorData.accel_z);
    Serial.println("========================================");
  }

  // 3. OLED page rotation -- also non-blocking, independent of sensor sampling
  if (now - lastPageChange >= PAGE_INTERVAL) {
    lastPageChange = now;
    page = (page + 1) % 3;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);

    if (page == 0) {
      display.println("-- ENVIRONMENT --");
      display.print("Temp: "); display.print(sensorData.temperature, 1); display.println(" C");
      display.print("Hum:  "); display.print(sensorData.humidity, 1); display.println(" %");
      display.print("Pres: "); display.print(sensorData.pressure, 0); display.println(" Pa");
    } else if (page == 1) {
      display.println("--- AIR QUALITY ---");
      display.print("MQ-135: "); display.println(sensorData.mq135_val);
      display.print("MQ-5:   "); display.println(sensorData.mq5_val);
    } else {
      display.println("- HEALTH & MOTION -");
      display.print("BPM: "); display.println(sensorData.bpm);
      display.print("X: "); display.print(sensorData.accel_x, 1);
      display.print("  Y: "); display.println(sensorData.accel_y, 1);
      display.print("Z: "); display.println(sensorData.accel_z, 1);
    }

    display.display();
  }
}
