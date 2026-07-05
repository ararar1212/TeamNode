#include "MAX30102Sensor.h"
#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <algorithm>

static MAX30105 particleSensor;

// ==================== CONFIGURATION ====================
static const unsigned long UPDATE_INTERVAL = 5000;     // Report every 5s (more responsive)
static const int BPM_BUFFER_SIZE = 12;                  // Larger buffer
static const int MIN_VALID_READINGS = 5;                // Need at least this many before reporting
static const int MIN_BPM = 40;
static const int MAX_BPM = 200;
static const float MAX_BPM_CHANGE_PER_UPDATE = 12.0;    // Prevent sudden jumps

// ==================== STATE ====================
static long lastBeat = 0;
static unsigned long lastUpdateTimer = 0;

static int bpmBuffer[BPM_BUFFER_SIZE] = {0};
static int bufferIndex = 0;
static int validReadings = 0;

static int lastReportedBPM = 0;           // For smoothing / hysteresis

bool max30102Init() {
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) return false;

    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
    
    // Optional: tweak for better signal on MAX30102
    particleSensor.setLEDMode(MAX30105_LED_MODE_RED_ONLY);
    particleSensor.setSamplingRate(400);           // Higher sampling = better beat detection
    particleSensor.setPulseWidth(411);             // 411us is good balance
    particleSensor.setADCResolution(2048);         // 18-bit
    
    return true;
}

long max30102ReadIR() {
    return particleSensor.getIR();
}

int max30102UpdateBpm(long irValue) {
    // 1. No finger / bad signal
    if (irValue < 50000) {
        validReadings = 0;
        bufferIndex = 0;
        lastReportedBPM = 0;
        return 0;
    }

    // 2. Beat detection
    if (checkForBeat(irValue)) {
        long delta = millis() - lastBeat;
        lastBeat = millis();

        if (delta > 300 && delta < 1500) {                 // ~40–200 BPM
            float newBpm = 60000.0f / delta;

            if (newBpm >= MIN_BPM && newBpm <= MAX_BPM) {
                bpmBuffer[bufferIndex] = (int)newBpm;
                bufferIndex = (bufferIndex + 1) % BPM_BUFFER_SIZE;
                if (validReadings < BPM_BUFFER_SIZE) validReadings++;
            }
        }
    }

    // 3. Time to compute stable BPM?
    if (millis() - lastUpdateTimer >= UPDATE_INTERVAL) {
        lastUpdateTimer = millis();

        if (validReadings < MIN_VALID_READINGS) {
            return lastReportedBPM;        // Keep showing last good value
        }

        // Copy and sort only the valid readings
        int sorted[BPM_BUFFER_SIZE];
        int count = 0;
        for (int i = 0; i < validReadings; i++) {
            if (bpmBuffer[i] >= MIN_BPM && bpmBuffer[i] <= MAX_BPM) {
                sorted[count++] = bpmBuffer[i];
            }
        }

        if (count < MIN_VALID_READINGS) return lastReportedBPM;

        std::sort(sorted, sorted + count);

        // Take median (very robust against outliers)
        int medianBpm = sorted[count / 2];

        // Optional: weighted average of middle values for extra smoothness
        if (count >= 5) {
            int sum = 0;
            int start = count / 2 - 1;
            int end = count / 2 + 2;
            for (int i = start; i < end && i < count; i++) {
                if (i >= 0) sum += sorted[i];
            }
            medianBpm = sum / (end - start);
        }

        // === Strong smoothing / rate limiting ===
        if (lastReportedBPM == 0) {
            lastReportedBPM = medianBpm;
        } else {
            // Limit how fast BPM can change
            float diff = medianBpm - lastReportedBPM;
            if (diff > MAX_BPM_CHANGE_PER_UPDATE) {
                lastReportedBPM += (int)MAX_BPM_CHANGE_PER_UPDATE;
            } else if (diff < -MAX_BPM_CHANGE_PER_UPDATE) {
                lastReportedBPM -= (int)MAX_BPM_CHANGE_PER_UPDATE;
            } else {
                // Exponential smoothing for natural feel
                lastReportedBPM = (int)(0.65f * lastReportedBPM + 0.35f * medianBpm);
            }
        }

        return lastReportedBPM;
    }

    return lastReportedBPM;   // Return last stable value between updates
}