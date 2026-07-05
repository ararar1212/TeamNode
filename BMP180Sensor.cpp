#include "BMP180Sensor.h"
#include <Arduino.h>
#include <Adafruit_BMP085.h>

static Adafruit_BMP085 bmp;

bool bmp180Init() {
  return bmp.begin();
}

float bmp180ReadPressure() {
  return bmp.readPressure();
}
