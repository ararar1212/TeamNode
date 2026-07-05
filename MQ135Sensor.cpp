#include "MQ135Sensor.h"
#include <Arduino.h>

#define MQ135_PIN 34

void mq135Init() {
  // No init required; call reserved for future calibration/warm-up logic.
}

int mq135Read() {
  return analogRead(MQ135_PIN);
}
