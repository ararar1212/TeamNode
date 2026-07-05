#ifndef MAX30102_SENSOR_H
#define MAX30102_SENSOR_H

// Returns false if the sensor isn't found on the I2C bus.
bool max30102Init();

// Raw IR reading. Call every loop iteration with no delay -- beat detection
// needs the full waveform, not an occasional sample of it.
long max30102ReadIR();

// Runs beat detection on the given IR reading and returns the current BPM.
// BPM holds its last valid value between beats and resets to 0 once the IR
// value drops below the "no finger/helmet contact" threshold.
int max30102UpdateBpm(long irValue);

#endif
