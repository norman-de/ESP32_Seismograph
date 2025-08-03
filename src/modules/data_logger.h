#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

// Forward declarations
class MQTTHandler;

struct LogEntry {
    unsigned long timestamp;
    String eventType;
    String description;
    float magnitude;
    String formattedTime;
};

struct SeismicEventData {
    // Detection info
    unsigned long timestamp;
    String datetimeISO;
    bool ntpValidated;
    unsigned long bootTimeMs;
    
    // Classification
    String eventType;
    int intensityLevel;
    String richterRange;
    float confidence;
    
    // Measurements
    float pgaG;
    float richterMagnitude;
    float localMagnitude;
    unsigned long durationMs;
    float peakFrequencyHz;
    float energyJoules;
    
    // Sensor data
    float maxAccelX;
    float maxAccelY;
    float maxAccelZ;
    float vectorMagnitude;
    bool calibrationValid;
    float calibrationAgeHours;
    
    // Algorithm data
    String detectionMethod;
    float triggerRatio;
    int staWindowSamples;
    int ltaWindowSamples;
    float backgroundNoise;
    
    // Metadata
    String source;
    String processingVersion;
    int sampleRateHz;
    String filterApplied;
    String dataQuality;
};

class DataLogger {
public:
    bool detailedLoggingEnabled;

private:
    bool initialized;
    String currentLogFile;
    unsigned long lastCleanup;
    MQTTHandler* mqttHandlerRef;
    
    // Private methods
    String generateLogFileName();
    String formatTimestamp(unsigned long timestamp);
    String formatUnixTimestamp(unsigned long unixTimestamp);
    bool writeToFile(const String& filename, const String& data);
    String readFromFile(const String& filename);
    void cleanupOldFiles();
    bool createDirectoryIfNotExists(const String& path);

public:
    DataLogger();
    void setDetailedLogging(bool enabled);
    bool begin();
    bool logEvent(const String& eventType, const String& description, float magnitude);
    bool logSeismicEvent(const SeismicEventData& eventData);
    bool logSystemEvent(const String& eventType, const String& description, float value);
    bool logSensorData(float accelX, float accelY, float accelZ, float magnitude);
    String getEventsJson(int maxEvents = 50);
    String getSeismicEventsJson(int maxEvents = 50);
    String getSystemEventsJson(int maxEvents = 50);
    String getFullSeismicEventsJson(int maxEvents = 100);
    String getSystemInfoJson();
    void printStorageInfo();
    bool deleteOldData(int daysToKeep = DATA_RETENTION_DAYS);
    
    // Reference management
    void setMQTTReference(MQTTHandler* mqttHandler);
};

#endif // DATA_LOGGER_H
