#ifndef TELEMETRY_PACKET_H
#define TELEMETRY_PACKET_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
  char worker_id[16];
  char worker_name[24];

  float temperature;
  float humidity;
  int32_t mq135_val;
  int32_t mq5_val;
  float altitude;

  float accel_x;
  float accel_y;
  float accel_z;
  int32_t motion;

  int32_t bpm;
  int32_t ir_value;

  int32_t risk_score;     // 0-100, computed on the sender
  uint8_t buzzer_active;  // 1 if the helmet buzzer is currently sounding, else 0

  uint32_t sequence;
  uint32_t uptime_ms;
} TelemetryPacket;

#endif