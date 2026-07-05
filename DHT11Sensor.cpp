#include "DHT11Sensor.h"
#include <Arduino.h>
#include <DHT.h>

#define DHT11_PIN  4
#define DHT11_TYPE DHT11

static DHT dht(DHT11_PIN, DHT11_TYPE);

void dht11Init() {
  dht.begin();
}

void dht11Read(float &temperature, float &humidity) {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
}
