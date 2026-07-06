#include "MPU6050Sensor.h"
#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

static Adafruit_MPU6050 mpu;

bool mpu6050Init() {
  if (!mpu.begin()) {
    return false;
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  return true;
}

void mpu6050Read(float &x, float &y, float &z) {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);
  x = accel.acceleration.x;
  y = accel.acceleration.y;
  z = accel.acceleration.z;
}
