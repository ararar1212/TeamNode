#ifndef BMP180_SENSOR_H
#define BMP180_SENSOR_H

// Returns false if the sensor isn't found on the I2C bus.
bool bmp180Init();

// Pressure in Pa.
float bmp180ReadPressure();

#endif
