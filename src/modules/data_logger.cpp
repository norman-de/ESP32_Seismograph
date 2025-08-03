#include "data_logger.h"
#include "time_manager.h"
#include "mqtt_handler.h"

// Externe Referenz auf TimeManager
extern TimeManager timeManager;

DataLogger::DataLogger() {
    initialized = false;
    currentLogFile = "";
    lastCleanup = 0;
    detailedLoggingEnabled = false; // Default to non-detailed logging
    mqttHandlerRef = nullptr;
}

void DataLogger::setDetailedLogging(bool enabled) {
    detailedLoggingEnabled = enabled;
}

bool DataLogger::begin() {
    if (!LittleFS.begin()) {
        Serial.println("ERROR: LittleFS not mounted");
        return false;
    }
    
    // Create necessary directories
    if (!createDirectoryIfNotExists("/logs")) {
        Serial.println("ERROR: Could not create logs directory");
        return false;
    }
    
    if (!createDirectoryIfNotExists("/events")) {
        Serial.println("ERROR: Could not create events directory");
        return false;
    }
    
    if (!createDirectoryIfNotExists("/data")) {
        Serial.println("ERROR: Could not create data directory");
        return false;
    }
    
    currentLogFile = generateLogFileName();
    initialized = true;
    
    Serial.println("Data Logger initialized successfully");
    printStorageInfo();
    
    // Log initialization
    logEvent("SYSTEM", "Data Logger initialized", 0);
    
    return true;
}

bool DataLogger::logEvent(const String& eventType, const String& description, float magnitude) {
    if (!initialized) {
        Serial.println("ERROR: Data Logger not initialized");
        return false;
    }
    
    // KRITISCH: Prüfe ob NTP-Zeit gültig ist für seismologische Events
    bool isSeismicEvent = (eventType == "Micro" || eventType == "Light" || eventType == "Strong");
    
    if (isSeismicEvent && !timeManager.isTimeValid()) {
        if (detailedLoggingEnabled) {
            Serial.println("CRITICAL: NTP time not valid - REJECTING seismic event logging for data integrity");
            Serial.printf("Seismic event rejected: %s - %s (%.4f)\n", 
                          eventType.c_str(), description.c_str(), magnitude);
        }
        
        // Log rejection für Audit-Trail (System-Events können auch ohne NTP geloggt werden)
        logSystemEvent("EVENT_REJECTED", "Seismic event rejected due to invalid NTP time: " + eventType, magnitude);
        return false;
    }
    
    // Verwende validierte NTP-Zeit oder Boot-Zeit für System-Events
    unsigned long unixTimestamp;
    bool ntpValid = timeManager.isTimeValid();
    
    if (ntpValid) {
        unixTimestamp = timeManager.getEpochTime();
    } else {
        // Für System-Events: verwende time(nullptr) mit Fallback-Validierung
        unixTimestamp = time(nullptr);
        if (unixTimestamp < 1577836800) { // 2020-01-01 00:00:00 UTC
            Serial.println("WARNING: No valid time available, skipping event logging");
            return false;
        }
    }
    
    // Create JSON object mit NTP-Validierungsmarkierung - OPTIMIZED with reserved capacity
    JsonDocument doc;
    doc["timestamp"] = unixTimestamp;
    doc["type"] = eventType;
    doc["description"] = description;
    doc["magnitude"] = magnitude;
    doc["ntp_valid"] = ntpValid;
    
    String jsonString;
    jsonString.reserve(256); // Pre-allocate memory to avoid reallocations
    serializeJson(doc, jsonString);
    
    // Write to events file
    String eventFile = "/events/" + String(millis() / 86400000) + ".json"; // Daily files
    
    File file = LittleFS.open(eventFile, "a");
    if (!file) {
        Serial.println("ERROR: Could not open event file for writing");
        return false;
    }
    
    file.println(jsonString);
    file.close();
    
    // Log successful event mit NTP-Status
    if (detailedLoggingEnabled) {
        if (ntpValid) {
            Serial.printf("[NTP-VALIDATED] %s: %s (%.4f) at %s\n", 
                          eventType.c_str(), description.c_str(), magnitude,
                          timeManager.getFormattedDateTime().c_str());
        } else {
            Serial.printf("[BOOT-TIME] %s: %s (%.4f)\n", 
                          eventType.c_str(), description.c_str(), magnitude);
        }
    }
    
    return true;
}

bool DataLogger::logSeismicEvent(const SeismicEventData& eventData) {
    if (!initialized) {
        Serial.println("ERROR: Data Logger not initialized");
        return false;
    }
    
    // KRITISCH: Nur seismische Events mit gültiger NTP-Zeit speichern
    if (!eventData.ntpValidated) {
        if (detailedLoggingEnabled) {
            Serial.println("CRITICAL: NTP time not valid - REJECTING seismic event for data integrity");
            Serial.printf("Seismic event rejected: %s - Richter %.2f\n", 
                          eventData.eventType.c_str(), eventData.richterMagnitude);
        }
        return false;
    }
    
    // Erstelle vollständige wissenschaftliche JSON-Struktur
    JsonDocument doc;
    
    // Event ID generieren
    char eventId[64];
    struct tm* timeInfo = localtime((time_t*)&eventData.timestamp);
    sprintf(eventId, "seismic_%04d%02d%02d_%02d%02d%02d_%03d",
            timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday,
            timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec,
            (int)(eventData.bootTimeMs % 1000));
    
    doc["event_id"] = eventId;
    
    // Detection section
    JsonObject detection = doc["detection"].to<JsonObject>();
    detection["timestamp"] = eventData.timestamp;
    detection["datetime_iso"] = eventData.datetimeISO;
    detection["ntp_validated"] = eventData.ntpValidated;
    detection["boot_time_ms"] = eventData.bootTimeMs;
    
    // Classification section
    JsonObject classification = doc["classification"].to<JsonObject>();
    classification["type"] = eventData.eventType;
    classification["intensity_level"] = eventData.intensityLevel;
    classification["richter_range"] = eventData.richterRange;
    classification["confidence"] = eventData.confidence;
    
    // Measurements section
    JsonObject measurements = doc["measurements"].to<JsonObject>();
    measurements["pga_g"] = eventData.pgaG;
    measurements["richter_magnitude"] = eventData.richterMagnitude;
    measurements["local_magnitude"] = eventData.localMagnitude;
    measurements["duration_ms"] = eventData.durationMs;
    measurements["peak_frequency_hz"] = eventData.peakFrequencyHz;
    measurements["energy_joules"] = eventData.energyJoules;
    
    // Sensor data section
    JsonObject sensorData = doc["sensor_data"].to<JsonObject>();
    sensorData["max_accel_x"] = eventData.maxAccelX;
    sensorData["max_accel_y"] = eventData.maxAccelY;
    sensorData["max_accel_z"] = eventData.maxAccelZ;
    sensorData["vector_magnitude"] = eventData.vectorMagnitude;
    sensorData["calibration_valid"] = eventData.calibrationValid;
    sensorData["calibration_age_hours"] = eventData.calibrationAgeHours;
    
    // Detection algorithm section
    JsonObject algorithm = doc["detection_algorithm"].to<JsonObject>();
    algorithm["method"] = eventData.detectionMethod;
    algorithm["trigger_ratio"] = eventData.triggerRatio;
    algorithm["sta_window_samples"] = eventData.staWindowSamples;
    algorithm["lta_window_samples"] = eventData.ltaWindowSamples;
    algorithm["background_noise"] = eventData.backgroundNoise;
    
    // Metadata section
    JsonObject metadata = doc["metadata"].to<JsonObject>();
    metadata["source"] = eventData.source;
    metadata["processing_version"] = eventData.processingVersion;
    metadata["sample_rate_hz"] = eventData.sampleRateHz;
    metadata["filter_applied"] = eventData.filterApplied;
    metadata["data_quality"] = eventData.dataQuality;
    
    // Serialize JSON
    String jsonString;
    jsonString.reserve(1024); // Pre-allocate für große JSON-Struktur
    serializeJson(doc, jsonString);
    
    // Speichere in separatem seismic-Ordner
    createDirectoryIfNotExists("/seismic");
    String seismicFile = "/seismic/" + String(eventData.timestamp / 86400) + ".json"; // Daily files
    
    File file = LittleFS.open(seismicFile, "a");
    if (!file) {
        Serial.println("ERROR: Could not open seismic event file for writing");
        return false;
    }
    
    file.println(jsonString);
    file.close();
    
    // Log successful seismic event
    if (detailedLoggingEnabled) {
        Serial.printf("[SEISMIC-EVENT] %s: Richter %.2f, PGA %.6fg at %s\n", 
                      eventData.eventType.c_str(), eventData.richterMagnitude, 
                      eventData.pgaG, eventData.datetimeISO.c_str());
        Serial.printf("Event ID: %s\n", eventId);
    }
    
    // Automatische MQTT-Publikation der vollständigen seismischen Event-Daten
    if (mqttHandlerRef && mqttHandlerRef->isConnected()) {
        bool mqttSuccess = mqttHandlerRef->publishSeismicEvent(eventData);
        if (detailedLoggingEnabled) {
            if (mqttSuccess) {
                Serial.printf("[MQTT] Seismic event published: %s\n", eventId);
            } else {
                Serial.printf("[MQTT] Failed to publish seismic event: %s\n", eventId);
            }
        }
    } else {
        if (detailedLoggingEnabled) {
            Serial.println("[MQTT] Not connected - seismic event not published");
        }
    }
    
    return true;
}

// Separate Methode für System-Events (können auch ohne NTP geloggt werden)
bool DataLogger::logSystemEvent(const String& eventType, const String& description, float value) {
    if (!initialized) return false;
    
    JsonDocument doc;
    
    if (timeManager.isTimeValid()) {
        doc["timestamp"] = timeManager.getEpochTime();
        doc["ntp_valid"] = true;
    } else {
        unsigned long unixTimestamp = time(nullptr);
        if (unixTimestamp >= 1577836800) { // Valid fallback time
            doc["timestamp"] = unixTimestamp;
        } else {
            doc["timestamp"] = millis(); // Boot-relative Zeit
        }
        doc["ntp_valid"] = false;
    }
    
    doc["type"] = eventType;
    doc["description"] = description;
    doc["value"] = value;
    
    String jsonString;
    jsonString.reserve(256); // Pre-allocate memory
    serializeJson(doc, jsonString);
    
    String systemFile = "/system/" + String(millis() / 86400000) + ".json";
    
    // Create system directory if needed
    createDirectoryIfNotExists("/system");
    
    File file = LittleFS.open(systemFile, "a");
    if (!file) return false;
    
    file.println(jsonString);
    file.close();
    
    if (detailedLoggingEnabled) {
        Serial.printf("[SYSTEM] %s: %s (%.4f)\n", eventType.c_str(), description.c_str(), value);
    }
    
    return true;
}

bool DataLogger::logSensorData(float accelX, float accelY, float accelZ, float magnitude) {
    if (!initialized) return false;
    
    // Only log sensor data periodically to avoid filling storage
    static unsigned long lastSensorLog = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastSensorLog < 1000) { // Log every second
        return true;
    }
    lastSensorLog = currentTime;
    
    JsonDocument doc;
    doc["timestamp"] = currentTime;
    doc["accel_x"] = accelX;
    doc["accel_y"] = accelY;
    doc["accel_z"] = accelZ;
    doc["magnitude"] = magnitude;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    String dataFile = "/data/" + String(currentTime / 86400000) + ".json"; // Daily files
    
    File file = LittleFS.open(dataFile, "a");
    if (!file) {
        return false;
    }
    
    file.println(jsonString);
    file.close();
    
    return true;
}

String DataLogger::getEventsJson(int maxEvents) {
    if (!initialized) return "[]";
    
    JsonDocument doc;
    JsonArray events = doc.to<JsonArray>();
    
    // Read events from files (most recent first)
    File root = LittleFS.open("/events");
    if (!root || !root.isDirectory()) {
        return "[]";
    }
    
    File file = root.openNextFile();
    int eventCount = 0;
    
    while (file && eventCount < maxEvents) {
        if (!file.isDirectory()) {
            String content = file.readString();
            
            // Parse each line as a JSON event
            int startPos = 0;
            int endPos = content.indexOf('\n');
            
            while (endPos != -1 && eventCount < maxEvents) {
                String line = content.substring(startPos, endPos);
                line.trim();
                
                if (line.length() > 0) {
                    JsonDocument eventDoc;
                    if (deserializeJson(eventDoc, line) == DeserializationError::Ok) {
                        events.add(eventDoc);
                        eventCount++;
                    }
                }
                
                startPos = endPos + 1;
                endPos = content.indexOf('\n', startPos);
            }
        }
        file = root.openNextFile();
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String DataLogger::getSeismicEventsJson(int maxEvents) {
    if (!initialized) return "[]";
    
    JsonDocument doc;
    JsonArray events = doc.to<JsonArray>();
    
    // Read events from files and filter for seismic events only
    File root = LittleFS.open("/events");
    if (!root || !root.isDirectory()) {
        return "[]";
    }
    
    File file = root.openNextFile();
    int eventCount = 0;
    
    while (file && eventCount < maxEvents) {
        if (!file.isDirectory()) {
            String content = file.readString();
            
            // Parse each line as a JSON event
            int startPos = 0;
            int endPos = content.indexOf('\n');
            
            while (endPos != -1 && eventCount < maxEvents) {
                String line = content.substring(startPos, endPos);
                line.trim();
                
                if (line.length() > 0) {
                    JsonDocument eventDoc;
                    if (deserializeJson(eventDoc, line) == DeserializationError::Ok) {
                        String eventType = eventDoc["type"].as<String>();
                        // Only include seismic events (Micro, Light, Strong)
                        if (eventType == "Micro" || eventType == "Light" || eventType == "Strong") {
                            events.add(eventDoc);
                            eventCount++;
                        }
                    }
                }
                
                startPos = endPos + 1;
                endPos = content.indexOf('\n', startPos);
            }
        }
        file = root.openNextFile();
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String DataLogger::getSystemEventsJson(int maxEvents) {
    if (!initialized) return "[]";
    
    JsonDocument doc;
    JsonArray events = doc.to<JsonArray>();
    
    // Read events from files and filter for system events only
    File root = LittleFS.open("/events");
    if (!root || !root.isDirectory()) {
        return "[]";
    }
    
    File file = root.openNextFile();
    int eventCount = 0;
    
    while (file && eventCount < maxEvents) {
        if (!file.isDirectory()) {
            String content = file.readString();
            
            // Parse each line as a JSON event
            int startPos = 0;
            int endPos = content.indexOf('\n');
            
            while (endPos != -1 && eventCount < maxEvents) {
                String line = content.substring(startPos, endPos);
                line.trim();
                
                if (line.length() > 0) {
                    JsonDocument eventDoc;
                    if (deserializeJson(eventDoc, line) == DeserializationError::Ok) {
                        String eventType = eventDoc["type"].as<String>();
                        // Exclude seismic events - include everything else
                        if (eventType != "Micro" && eventType != "Light" && eventType != "Strong") {
                            events.add(eventDoc);
                            eventCount++;
                        }
                    }
                }
                
                startPos = endPos + 1;
                endPos = content.indexOf('\n', startPos);
            }
        }
        file = root.openNextFile();
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String DataLogger::getFullSeismicEventsJson(int maxEvents) {
    if (!initialized) return "[]";
    
    JsonDocument doc;
    JsonArray events = doc.to<JsonArray>();
    
    // Read seismic events from dedicated /seismic/ directory
    File root = LittleFS.open("/seismic");
    if (!root || !root.isDirectory()) {
        // Fallback: create empty response with metadata
        JsonDocument response;
        response["events"] = events;
        response["total_count"] = 0;
        response["message"] = "No seismic events directory found";
        
        String result;
        serializeJson(response, result);
        return result;
    }
    
    File file = root.openNextFile();
    int eventCount = 0;
    
    // Statistics tracking
    JsonObject stats = doc["statistics"].to<JsonObject>();
    JsonObject byType = stats["by_type"].to<JsonObject>();
    byType["Micro"] = 0;
    byType["Minor"] = 0;
    byType["Light"] = 0;
    byType["Moderate"] = 0;
    byType["Strong"] = 0;
    byType["Major"] = 0;
    
    float minRichter = 10.0f;
    float maxRichter = 0.0f;
    float totalRichter = 0.0f;
    int richterCount = 0;
    
    while (file && eventCount < maxEvents) {
        if (!file.isDirectory()) {
            String content = file.readString();
            
            // Parse each line as a complete seismic event JSON
            int startPos = 0;
            int endPos = content.indexOf('\n');
            
            while (endPos != -1 && eventCount < maxEvents) {
                String line = content.substring(startPos, endPos);
                line.trim();
                
                if (line.length() > 0) {
                    JsonDocument eventDoc;
                    if (deserializeJson(eventDoc, line) == DeserializationError::Ok) {
                        events.add(eventDoc);
                        eventCount++;
                        
                        // Update statistics
                        String eventType = eventDoc["classification"]["type"].as<String>();
                        if (byType[eventType].is<int>()) {
                            byType[eventType] = byType[eventType].as<int>() + 1;
                        }
                        
                        float richter = eventDoc["measurements"]["richter_magnitude"].as<float>();
                        if (richter > 0) {
                            minRichter = min(minRichter, richter);
                            maxRichter = max(maxRichter, richter);
                            totalRichter += richter;
                            richterCount++;
                        }
                    }
                }
                
                startPos = endPos + 1;
                endPos = content.indexOf('\n', startPos);
            }
        }
        file = root.openNextFile();
    }
    
    // Create comprehensive response
    JsonDocument response;
    response["events"] = events;
    response["total_count"] = eventCount;
    
    // Time range (if events exist)
    if (eventCount > 0) {
        JsonObject timeRange = response["time_range"].to<JsonObject>();
        // Get first and last event timestamps
        unsigned long firstTime = events[0]["detection"]["timestamp"].as<unsigned long>();
        unsigned long lastTime = events[eventCount-1]["detection"]["timestamp"].as<unsigned long>();
        
        timeRange["from_timestamp"] = firstTime;
        timeRange["to_timestamp"] = lastTime;
        timeRange["from_iso"] = events[0]["detection"]["datetime_iso"].as<String>();
        timeRange["to_iso"] = events[eventCount-1]["detection"]["datetime_iso"].as<String>();
    }
    
    // Statistics
    response["statistics"] = stats;
    if (richterCount > 0) {
        JsonObject magnitudeRange = stats["magnitude_range"].to<JsonObject>();
        magnitudeRange["min_richter"] = minRichter;
        magnitudeRange["max_richter"] = maxRichter;
        magnitudeRange["avg_richter"] = totalRichter / richterCount;
        magnitudeRange["event_count"] = richterCount;
    }
    
    String result;
    result.reserve(2048); // Pre-allocate for large response
    serializeJson(response, result);
    return result;
}

String DataLogger::getSystemInfoJson() {
    JsonDocument doc;
    
    doc["total_space"] = LittleFS.totalBytes();
    doc["used_space"] = LittleFS.usedBytes();
    doc["free_space"] = LittleFS.totalBytes() - LittleFS.usedBytes();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["current_log_file"] = currentLogFile;
    
    String result;
    serializeJson(doc, result);
    return result;
}



void DataLogger::printStorageInfo() {
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;
    
    if (detailedLoggingEnabled) {
        Serial.println("=== Storage Information ===");
        Serial.printf("Total space: %d bytes (%.2f KB)\n", totalBytes, totalBytes / 1024.0);
        Serial.printf("Used space: %d bytes (%.2f KB)\n", usedBytes, usedBytes / 1024.0);
        Serial.printf("Free space: %d bytes (%.2f KB)\n", freeBytes, freeBytes / 1024.0);
        Serial.printf("Usage: %.1f%%\n", (usedBytes * 100.0) / totalBytes);
    }
}

bool DataLogger::deleteOldData(int daysToKeep) {
    unsigned long cutoffTime = millis() - (daysToKeep * 86400000UL);
    unsigned long cutoffDay = cutoffTime / 86400000;
    
    if (detailedLoggingEnabled) Serial.printf("Cleaning up data older than %d days (day %lu)\n", daysToKeep, cutoffDay);
    
    // Clean events directory
    File eventsDir = LittleFS.open("/events");
    if (eventsDir && eventsDir.isDirectory()) {
        File file = eventsDir.openNextFile();
        while (file) {
            String fileName = file.name();
            unsigned long fileDay = fileName.substring(fileName.lastIndexOf('/') + 1, fileName.indexOf('.')).toInt();
            
            if (fileDay < cutoffDay) {
                String fullPath = "/events/" + fileName;
                LittleFS.remove(fullPath);
                if (detailedLoggingEnabled) Serial.printf("Deleted old event file: %s\n", fullPath.c_str());
            }
            file = eventsDir.openNextFile();
        }
    }
    
    // Clean data directory
    File dataDir = LittleFS.open("/data");
    if (dataDir && dataDir.isDirectory()) {
        File file = dataDir.openNextFile();
        while (file) {
            String fileName = file.name();
            unsigned long fileDay = fileName.substring(fileName.lastIndexOf('/') + 1, fileName.indexOf('.')).toInt();
            
            if (fileDay < cutoffDay) {
                String fullPath = "/data/" + fileName;
                LittleFS.remove(fullPath);
                if (detailedLoggingEnabled) Serial.printf("Deleted old data file: %s\n", fullPath.c_str());
            }
            file = dataDir.openNextFile();
        }
    }
    
    return true;
}

String DataLogger::generateLogFileName() {
    unsigned long day = millis() / 86400000; // Days since boot
    return "/logs/log_" + String(day) + ".txt";
}

String DataLogger::formatTimestamp(unsigned long timestamp) {
    unsigned long seconds = timestamp / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    char buffer[32];
    sprintf(buffer, "%02lu:%02lu:%02lu.%03lu", 
            hours, minutes, seconds, timestamp % 1000);
    
    return String(buffer);
}

String DataLogger::formatUnixTimestamp(unsigned long unixTimestamp) {
    // Convert Unix timestamp to readable format
    if (unixTimestamp == 0) {
        return "N/A";
    }
    
    // Simple conversion - this could be enhanced with proper timezone handling
    time_t rawTime = (time_t)unixTimestamp;
    struct tm* timeInfo = localtime(&rawTime);
    
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeInfo);
    
    return String(buffer);
}

bool DataLogger::writeToFile(const String& filename, const String& data) {
    File file = LittleFS.open(filename, "w");
    if (!file) {
        return false;
    }
    
    file.print(data);
    file.close();
    return true;
}

String DataLogger::readFromFile(const String& filename) {
    File file = LittleFS.open(filename, "r");
    if (!file) {
        return "";
    }
    
    String content = file.readString();
    file.close();
    return content;
}

void DataLogger::cleanupOldFiles() {
    unsigned long currentTime = millis();
    
    // Only cleanup once per hour
    if (currentTime - lastCleanup < 3600000) {
        return;
    }
    
    lastCleanup = currentTime;
    deleteOldData(DATA_RETENTION_DAYS);
}

bool DataLogger::createDirectoryIfNotExists(const String& path) {
    if (!LittleFS.exists(path)) {
        return LittleFS.mkdir(path);
    }
    return true;
}

void DataLogger::setMQTTReference(MQTTHandler* mqttHandler) {
    mqttHandlerRef = mqttHandler;
    if (detailedLoggingEnabled) {
        Serial.println("MQTT reference set in DataLogger");
    }
}
