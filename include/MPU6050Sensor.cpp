#include "MPU6050Sensor.h"
#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>

static Adafruit_MPU6050 mpu;

// ============= EXISTING FUNCTIONS =============

bool mpu6050Init() {
  if (!mpu.begin()) {
    return false;
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  
  // Initialize fall detector
  fallDetectorInit();
  
  return true;
}

void mpu6050Read(float &x, float &y, float &z) {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);
  x = accel.acceleration.x;
  y = accel.acceleration.y;
  z = accel.acceleration.z;
}

// ============= FALL DETECTION IMPLEMENTATION =============

// Fall detection thresholds (tunable)
static const float FREEFALL_THRESHOLD_G = 0.8f;      // Below this = freefall
static const float IMPACT_THRESHOLD_G = 2.5f;        // Above this after fall = impact
static const int FREEFALL_DURATION_MS = 150;         // Time in freefall to trigger detection
static const int POST_FALL_WINDOW_MS = 500;          // Window for impact after free-fall

// State machine
static bool freefallDetected = false;
static unsigned long freefallStart = 0;
static bool inPostFallWindow = false;
static unsigned long postFallStart = 0;
static bool fallConfirmed = false;
static unsigned long fallConfirmedTime = 0;

void fallDetectorInit() {
  freefallDetected = false;
  inPostFallWindow = false;
  fallConfirmed = false;
}

bool detectFall(float ax, float ay, float az) {
  // Calculate magnitude of acceleration in G
  const float accelMagnitude = sqrtf(ax * ax + ay * ay + az * az) / 9.81f;
  const unsigned long now = millis();

  // ===== STAGE 1: DETECT FREE-FALL =====
  // Free-fall = acceleration close to 0G (just gravity cancelled out)
  if (accelMagnitude < FREEFALL_THRESHOLD_G) {
    if (!freefallDetected) {
      freefallDetected = true;
      freefallStart = now;
    }

    // If we've been in freefall long enough, enter post-fall impact window
    if (now - freefallStart >= FREEFALL_DURATION_MS) {
      if (!inPostFallWindow) {
        inPostFallWindow = true;
        postFallStart = now;
      }
    }
  } else {
    // Exited freefall state
    freefallDetected = false;
  }

  // ===== STAGE 2: DETECT IMPACT =====
  // Impact = sudden high acceleration during the post-fall window
  if (inPostFallWindow) {
    if (accelMagnitude > IMPACT_THRESHOLD_G) {
      // Fall confirmed! Set state and return true
      fallConfirmed = true;
      fallConfirmedTime = now;
      inPostFallWindow = false;
      freefallDetected = false;
      
      // Debug output
      Serial.printf("[FALL DETECTED] Magnitude: %.2f G at time %lu ms\n", 
                    accelMagnitude, now);
      
      return true;
    }

    // Timeout: if no impact occurs, exit post-fall window
    if (now - postFallStart > POST_FALL_WINDOW_MS) {
      inPostFallWindow = false;
      freefallDetected = false;
    }
  }

  return false;
}

bool isFalling() {
  // A fall remains "active" for 3 seconds after detection
  // After 3 seconds, auto-reset the state
  if (!fallConfirmed) {
    return false;
  }
  
  if (millis() - fallConfirmedTime > 3000) {
    fallConfirmed = false;
  }
  
  return fallConfirmed;
}

void fallResetState() {
  fallConfirmed = false;
  inPostFallWindow = false;
  freefallDetected = false;
}