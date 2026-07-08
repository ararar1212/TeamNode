#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <math.h>
#include <string.h>

#include "TelemetryPacket.h"

static TelemetryPacket latestPacket;
static volatile bool packetReady = false;
static portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED;

static float sanitizeFloat(float value) {
  if (isnan(value) || isinf(value)) {
    return 0.0f;
  }
  return value;
}

static int sanitizeInt(int32_t value) {
  return value;
}

static void onDataRecv(const uint8_t *macAddr, const uint8_t *data, int len) {
  (void)macAddr;
  if (len != (int)sizeof(TelemetryPacket)) {
    return;
  }

  portENTER_CRITICAL_ISR(&packetMux);
  memcpy(&latestPacket, data, sizeof(TelemetryPacket));
  packetReady = true;
  portEXIT_CRITICAL_ISR(&packetMux);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  Serial.println("Receiver ready. Waiting for packets...");
}

void loop() {
  if (!packetReady) {
    delay(5);
    return;
  }

  TelemetryPacket packet;
  portENTER_CRITICAL(&packetMux);
  packet = latestPacket;
  packetReady = false;
  portEXIT_CRITICAL(&packetMux);

  const float ambientTemp = sanitizeFloat(packet.temperature);
  const float humidity = sanitizeFloat(packet.humidity);
  const int pulse = sanitizeInt(packet.bpm);
  const int motion = sanitizeInt(packet.motion);
  const int fallDetected = sanitizeInt(packet.fall_detected);
  const int sosAlert = sanitizeInt(packet.sos_alert);

  // JSON line consumed by the Node server serial bridge.
  // ===== NEW: Added fall_detected and sos_alert fields =====
  Serial.print("{\"worker_id\":\"");
  Serial.print(packet.worker_id);
  Serial.print("\",\"worker_name\":\"");
  Serial.print(packet.worker_name);
  Serial.printf("\",\"pulse\":%d,\"ambient_temp\":%.2f,\"humidity\":%.2f,\"motion\":%d,\"fall_detected\":%d,\"sos_alert\":%d",
    pulse, ambientTemp, humidity, motion, fallDetected, sosAlert);
  Serial.printf(",\"mq135\":%ld,\"mq5\":%ld,\"altitude\":%.2f",
    (long)packet.mq135_val, (long)packet.mq5_val, sanitizeFloat(packet.altitude));
  Serial.printf(",\"accel_x\":%.3f,\"accel_y\":%.3f,\"accel_z\":%.3f",
    sanitizeFloat(packet.accel_x), sanitizeFloat(packet.accel_y), sanitizeFloat(packet.accel_z));
  Serial.printf(",\"sequence\":%lu,\"gps_lat\":%.6f,\"gps_lng\":%.6f}\n",
    (unsigned long)packet.sequence, sanitizeFloat(packet.gps_lat), sanitizeFloat(packet.gps_lng));
}
