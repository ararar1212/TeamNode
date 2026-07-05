#ifndef MQ135_SENSOR_H
#define MQ135_SENSOR_H

// Placeholder for future warm-up/calibration logic -- MQ135 needs no setup
// beyond the ADC being ready, but the symmetry keeps setup() uniform.
void mq135Init();

// Raw ADC reading, 0-4095 on the ESP32's 12-bit ADC.
int mq135Read();

#endif
