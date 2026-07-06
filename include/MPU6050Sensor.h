#ifndef MPU6050_SENSOR_H
#define MPU6050_SENSOR_H

// Returns false if the sensor isn't found on the I2C bus.
bool mpu6050Init();

// Acceleration on each axis, in m/s^2.
void mpu6050Read(float &x, float &y, float &z);

// ============= FALL DETECTION =============

// Initialize fall detection module
void fallDetectorInit();

// Call this with raw accelerometer data to detect falls
// Returns true if a fall event is detected in this frame
bool detectFall(float ax, float ay, float az);

// Get current fall state (true if recently fell, false otherwise)
bool isFalling();

// Reset fall state manually (e.g., after alerting)
void fallResetState();

#endif
