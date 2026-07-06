#include "MQ5Sensor.h"
#include <Arduino.h>

#define MQ5_PIN 35

void mq5Init() {
  // No init required; call reserved for future calibration/warm-up logic.
}

int mq5Read() {
  return analogRead(MQ5_PIN);
}
