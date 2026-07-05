#ifndef MPU6050_SENSOR_H
#define MPU6050_SENSOR_H

// Returns false if the sensor isn't found on the I2C bus.
bool mpu6050Init();

// Acceleration on each axis, in m/s^2.
void mpu6050Read(float &x, float &y, float &z);

#endif
