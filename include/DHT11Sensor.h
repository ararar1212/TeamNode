#ifndef DHT11_SENSOR_H
#define DHT11_SENSOR_H

// Initializes the DHT11 temperature/humidity sensor.
void dht11Init();

// Reads temperature (C) and humidity (%) into the given references.
// DHT11 returns NaN on a failed read -- check with isnan() if you need to
// guard against that.
void dht11Read(float &temperature, float &humidity);

#endif
