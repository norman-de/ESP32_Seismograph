#ifndef SEISMOGRAPH_H
#define SEISMOGRAPH_H

#include <Arduino.h>
#include <Wire.h>
#include <MPU6050.h>
#include "config.h"

struct SensorData {
    float accelX;
    float accelY;
    float accelZ;
    float magnitude;
    unsigned long timestamp;
};

struct SeismicEvent {
    unsigned long startTime;
    unsigned long endTime;
    float maxMagnitude;
    float avgMagnitude;
    int level; // 1=micro, 2=light, 3=strong
    String description;
};

class Seismograph {
private:
    MPU6050 mpu;
    bool initialized;
    
    // Calibration data
    float offsetX, offsetY, offsetZ;
    bool calibrated;
    
    // STA/LTA algorithm variables
    float staBuffer[STA_WINDOW];
    float ltaBuffer[LTA_WINDOW];
    int staIndex;
    int ltaIndex;
    float staSum;
    float ltaSum;
    bool staFull;
    bool ltaFull;
    
    // Event detection
    bool eventActive;
    unsigned long eventStartTime;
    float eventMaxMagnitude;
    float eventSumMagnitude;
    int eventSampleCount;
    
    // Adaptive thresholds
    float adaptiveThresholdMicro;
    float adaptiveThresholdLight;
    float adaptiveThresholdStrong;
    float backgroundNoise;
    unsigned long lastAdaptiveUpdate;
    bool adaptiveThresholdEnabled;
    
    // Spike filtering
    float lastMagnitudes[5]; // Buffer for spike detection
    int magnitudeIndex;
    bool magnitudeBufferFull;
    
    // Statistics
    unsigned long totalSamples;
    unsigned long eventsDetected;
    unsigned long spikesFiltered;
    float lastMagnitude;
    
    // Detailed logging configuration
    unsigned long detailedLoggingInterval;
    
    // Enhanced calibration monitoring
    float lastCalibrationOffsets[3]; // Store previous calibration for comparison
    unsigned long lastCalibrationTime;
    float baselineLTA; // Baseline LTA after calibration
    unsigned long lastDriftCheck;
    bool calibrationValid;
    
    // Private methods
    float calculateMagnitude(float x, float y, float z);
    void updateSTALTA(float magnitude);
    bool checkEventTrigger();
    void startEvent(float magnitude);
    void endEvent();
    int classifyEvent(float magnitude);
    void updateAdaptiveThresholds();
    bool isSpikeFiltered(float magnitude);
    float getMedianMagnitude();
    void checkCalibrationDrift();

public:
    bool detailedLoggingEnabled;
    Seismograph();
    bool begin();
    bool calibrate();
    SensorData readSensor();
    void processData(SensorData data);
    void simulateEvent(float magnitude);
    void printStats();
    bool isCalibrated() { return calibrated; }
    unsigned long getEventsDetected() { return eventsDetected; }
    float getLastMagnitude() { return lastMagnitude; }
    void setAdaptiveThresholdEnabled(bool enabled) { adaptiveThresholdEnabled = enabled; }
    bool isAdaptiveThresholdEnabled() { return adaptiveThresholdEnabled; }
    void setDetailedLoggingInterval(unsigned long intervalMs) { detailedLoggingInterval = intervalMs; }
    void enableDetailedLogging(bool enable) { detailedLoggingEnabled = enable; }
    
    // Scientific magnitude calculations
    float calculateRichterMagnitude(float acceleration);
    float calculateLocalMagnitude(float acceleration);
    String getScientificEventDescription(float magnitude, unsigned long duration);
    String getEventTypeFromRichter(float richter);
    
    // New seismic event creation
    void createSeismicEvent(float magnitude, unsigned long duration, const String& source);
    int getIntensityLevelFromRichter(float richter);
    String getRichterRangeFromType(const String& eventType);
    float calculateEnergyJoules(float richter);
    float calculatePeakFrequency(float magnitude);
    float getCalibrationAgeHours();
    
    // Simulation helper functions
    float calculatePGAFromRichter(float richter);
    unsigned long calculateEventDuration(float richter);
};

#endif // SEISMOGRAPH_H
