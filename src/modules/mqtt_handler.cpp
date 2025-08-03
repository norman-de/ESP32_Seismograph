#include "mqtt_handler.h"
#include "time_manager.h"
#include "seismograph.h"

// Global instance for callback
MQTTHandler* globalMQTTHandler = nullptr;

MQTTHandler::MQTTHandler() : mqttClient(wifiClient) {
    detailedLoggingEnabled = false;
    initialized = false;
    lastReconnectAttempt = 0;
    lastHeartbeat = 0;
    lastDataPublish = 0;
    lastStatusPublish = 0;
    timeManagerRef = nullptr;
    seismographRef = nullptr;
    debugModeEnabled = false;
    globalMQTTHandler = this;
}

bool MQTTHandler::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        if (detailedLoggingEnabled) Serial.println("ERROR: WiFi not connected, cannot initialize MQTT");
        return false;
    }
    
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(staticMessageCallback);
    
    // Set Last Will Testament
    setLastWillTestament();
    
    // Attempt initial connection
    if (reconnect()) {
        initialized = true;
        if (detailedLoggingEnabled) Serial.println("MQTT Handler initialized successfully");
        
        // Subscribe to command topics
        subscribe(String(TOPIC_COMMAND) + "#");
        
        // Send initial status
        publishStatus("{\"status\":\"online\",\"message\":\"MQTT connected\"}");
        
        return true;
    }
    
    if (detailedLoggingEnabled) Serial.println("MQTT initial connection failed, will retry in loop");
    return false;
}

void MQTTHandler::loop() {
    if (!initialized) return;
    
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > 5000) { // Try to reconnect every 5 seconds
            lastReconnectAttempt = now;
            if (reconnect()) {
                lastReconnectAttempt = 0;
            }
        }
    } else {
        mqttClient.loop();
        
        // Check scheduled publishing
        checkScheduledPublishing();
    }
}

bool MQTTHandler::isConnected() {
    return initialized && mqttClient.connected();
}

bool MQTTHandler::publishData(const String& data) {
    return publish(TOPIC_DATA, data);
}

bool MQTTHandler::publishEvent(const String& event) {
    return publish(TOPIC_EVENT, event, true); // Retain events
}

bool MQTTHandler::publishSeismicEvent(const SeismicEventData& eventData) {
    if (!isConnected()) {
        if (detailedLoggingEnabled) Serial.println("MQTT not connected, cannot publish seismic event");
        return false;
    }
    
    // Create comprehensive seismic event JSON
    JsonDocument doc;
    
    // Event ID generieren
    char eventId[64];
    struct tm* timeInfo = localtime((time_t*)&eventData.timestamp);
    sprintf(eventId, "seismic_%04d%02d%02d_%02d%02d%02d_%03d",
            timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday,
            timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec,
            (int)(eventData.bootTimeMs % 1000));
    
    doc["event_id"] = eventId;
    doc["device_id"] = MQTT_CLIENT_ID;
    
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
    
    // Serialize and publish
    String jsonString;
    jsonString.reserve(1024); // Pre-allocate for large JSON
    serializeJson(doc, jsonString);
    
    bool result = publish(TOPIC_EVENT, jsonString, true); // Retain seismic events
    
    if (detailedLoggingEnabled) {
        if (result) {
            Serial.printf("MQTT seismic event published: %s (Richter %.2f)\n", 
                          eventData.eventType.c_str(), eventData.richterMagnitude);
        } else {
            Serial.println("MQTT seismic event publish failed");
        }
    }
    
    return result;
}

bool MQTTHandler::publishStatus(const String& status) {
    return publish(TOPIC_STATUS, status, true); // Retain status
}

bool MQTTHandler::publish(const String& topic, const String& payload, bool retained) {
    if (!isConnected()) {
        if (detailedLoggingEnabled) Serial.printf("MQTT not connected, cannot publish to %s\n", topic.c_str());
        return false;
    }
    
    bool result = mqttClient.publish(topic.c_str(), payload.c_str(), retained);
    
    if (detailedLoggingEnabled) {
        if (result) {
            Serial.printf("MQTT published to %s: %s\n", topic.c_str(), payload.c_str());
        } else {
            Serial.printf("MQTT publish failed to %s\n", topic.c_str());
        }
    }
    
    return result;
}

bool MQTTHandler::subscribe(const String& topic) {
    if (!isConnected()) {
        if (detailedLoggingEnabled) Serial.printf("MQTT not connected, cannot subscribe to %s\n", topic.c_str());
        return false;
    }
    
    bool result = mqttClient.subscribe(topic.c_str());
    
    if (detailedLoggingEnabled) {
        if (result) {
            Serial.printf("MQTT subscribed to %s\n", topic.c_str());
        } else {
            Serial.printf("MQTT subscription failed to %s\n", topic.c_str());
        }
    }
    
    return result;
}

bool MQTTHandler::unsubscribe(const String& topic) {
    if (!isConnected()) {
        return false;
    }
    
    return mqttClient.unsubscribe(topic.c_str());
}

bool MQTTHandler::reconnect() {
    if (detailedLoggingEnabled) Serial.print("Attempting MQTT connection...");
    
    String clientId = String(MQTT_CLIENT_ID) + "_" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
        if (detailedLoggingEnabled) Serial.println(" connected!");
        
        // Resubscribe to topics
        subscribe(String(TOPIC_COMMAND) + "#");
        
        // Announce connection
        publishStatus("{\"status\":\"online\",\"message\":\"MQTT reconnected\"}");
        
        return true;
    } else {
        if (detailedLoggingEnabled) Serial.printf(" failed, rc=%d\n", mqttClient.state());
        return false;
    }
}

void MQTTHandler::staticMessageCallback(char* topic, byte* payload, unsigned int length) {
    if (globalMQTTHandler) {
        globalMQTTHandler->onMessageReceived(topic, payload, length);
    }
}

void MQTTHandler::onMessageReceived(char* topic, byte* payload, unsigned int length) {
    // Convert payload to string
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    String topicStr = String(topic);
    if (detailedLoggingEnabled) Serial.printf("MQTT message received on %s: %s\n", topic, message.c_str());
    
    // Process command if it's a command topic
    if (topicStr.startsWith(TOPIC_COMMAND)) {
        String command = topicStr.substring(strlen(TOPIC_COMMAND));
        processCommand(command, message);
    }
}

void MQTTHandler::processCommand(const String& command, const String& payload) {
    if (detailedLoggingEnabled) Serial.printf("Processing MQTT command: %s with payload: %s\n", command.c_str(), payload.c_str());
    
    if (command == "restart") {
        Serial.println("Restart command received via MQTT");
        publishStatus("{\"status\":\"restarting\",\"message\":\"Restart command received\"}");
        delay(1000);
        ESP.restart();
    }
    else if (command == "calibrate") {
        Serial.println("Calibrate command received via MQTT");
        if (seismographRef != nullptr) {
            publishStatus("{\"status\":\"calibrating\",\"message\":\"Calibration started\"}");
            bool success = seismographRef->calibrate();
            if (success) {
                publishStatus("{\"status\":\"calibrated\",\"message\":\"Sensor calibration successful\"}");
                Serial.println("MQTT calibration command completed successfully");
            } else {
                publishStatus("{\"status\":\"error\",\"message\":\"Sensor calibration failed\"}");
                Serial.println("MQTT calibration command failed");
            }
        } else {
            publishStatus("{\"status\":\"error\",\"message\":\"Seismograph not available for calibration\"}");
            Serial.println("MQTT calibration failed: Seismograph reference not set");
        }
    }
    else if (command == "debug") {
        Serial.println("Debug command received via MQTT");
        debugModeEnabled = !debugModeEnabled;
        
        // Apply debug mode to all components
        if (seismographRef != nullptr) {
            seismographRef->enableDetailedLogging(debugModeEnabled);
        }
        detailedLoggingEnabled = debugModeEnabled;
        
        String status = debugModeEnabled ? "enabled" : "disabled";
        publishStatus("{\"status\":\"debug\",\"message\":\"Debug mode " + status + "\"}");
        Serial.printf("MQTT debug mode %s\n", status.c_str());
    }
    else if (command == "status") {
        Serial.println("Status request received via MQTT");
        // Send detailed status
        sendHeartbeat();
    }
    else {
        Serial.printf("Unknown MQTT command: %s\n", command.c_str());
        publishStatus("{\"status\":\"error\",\"message\":\"Unknown command: " + command + "\"}");
    }
}

void MQTTHandler::sendHeartbeat() {
    if (!isConnected()) return;
    
    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["ip_address"] = WiFi.localIP().toString();
    doc["status"] = "online";
    
    // Add NTP timestamp if available
    if (timeManagerRef != nullptr) {
        doc["ntp_valid"] = timeManagerRef->isTimeValid();
        if (timeManagerRef->isTimeValid()) {
            doc["timestamp"] = timeManagerRef->getEpochTime();
        }
    } else {
        doc["ntp_valid"] = false;
    }
    
    String heartbeat;
    serializeJson(doc, heartbeat);
    
    publishStatus(heartbeat);
}

void MQTTHandler::setLastWillTestament() {
    // Note: PubSubClient doesn't support setWill in this version
    // LWT would need to be configured during connection
    if (detailedLoggingEnabled) Serial.println("LWT configuration skipped - not supported by current PubSubClient version");
}

String MQTTHandler::createDataJson(float accelX, float accelY, float accelZ, float magnitude) {
    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["accel_x"] = accelX;
    doc["accel_y"] = accelY;
    doc["accel_z"] = accelZ;
    doc["magnitude"] = magnitude;
    doc["device_id"] = MQTT_CLIENT_ID;
    
    // Add NTP timestamp if available
    if (timeManagerRef != nullptr) {
        doc["ntp_valid"] = timeManagerRef->isTimeValid();
        if (timeManagerRef->isTimeValid()) {
            doc["timestamp"] = timeManagerRef->getEpochTime();
        }
    } else {
        doc["ntp_valid"] = false;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

bool MQTTHandler::publishDataSummary(const String& summary) {
    unsigned long now = millis();
    if (now - lastDataPublish >= MQTT_DATA_INTERVAL) {
        lastDataPublish = now;
        if (detailedLoggingEnabled) Serial.println("Publishing scheduled data summary");
        return publishData(summary);
    }
    return false; // Not time yet
}

bool MQTTHandler::publishStatusUpdate(const String& status) {
    unsigned long now = millis();
    if (now - lastStatusPublish >= MQTT_STATUS_INTERVAL) {
        lastStatusPublish = now;
        if (detailedLoggingEnabled) Serial.println("Publishing scheduled status update");
        return publishStatus(status);
    }
    return false; // Not time yet
}

void MQTTHandler::checkScheduledPublishing() {
    unsigned long now = millis();
    
    // Check heartbeat interval
    if (now - lastHeartbeat >= MQTT_HEARTBEAT_INTERVAL) {
        sendHeartbeat();
        lastHeartbeat = now;
    }
    
    // Note: Data and status summaries are published via explicit calls
    // from the main application when data is available
}

void MQTTHandler::setTimeManagerReference(TimeManager* timeManager) {
    timeManagerRef = timeManager;
}

void MQTTHandler::setSeismographReference(Seismograph* seismograph) {
    seismographRef = seismograph;
}

String MQTTHandler::createEventJson(const String& eventType, float magnitude, int level) {
    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["event_type"] = eventType;
    doc["magnitude"] = magnitude;
    doc["level"] = level;
    doc["device_id"] = MQTT_CLIENT_ID;
    
    // Add NTP timestamp if available
    if (timeManagerRef != nullptr) {
        doc["ntp_valid"] = timeManagerRef->isTimeValid();
        if (timeManagerRef->isTimeValid()) {
            doc["timestamp"] = timeManagerRef->getEpochTime();
        }
    } else {
        doc["ntp_valid"] = false;
    }
    
    String levelStr;
    switch (level) {
        case 1: levelStr = "micro"; break;
        case 2: levelStr = "light"; break;
        case 3: levelStr = "strong"; break;
        default: levelStr = "unknown"; break;
    }
    doc["level_description"] = levelStr;
    
    String result;
    serializeJson(doc, result);
    return result;
}
