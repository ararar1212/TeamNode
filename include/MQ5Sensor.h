#ifndef MQ5_SENSOR_H
#define MQ5_SENSOR_H

void mq5Init();
int mq5Read();  // raw ADC value, 0-4095 on ESP32

#endif
