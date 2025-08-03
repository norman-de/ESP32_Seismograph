#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "data_logger.h"

// Forward declarations
class TimeManager;
class Seismograph;

class MQTTHandler {
public:
    bool detailedLoggingEnabled;
private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    bool initialized;
    unsigned long lastReconnectAttempt;
    unsigned long lastHeartbeat;
    unsigned long lastDataPublish;
    unsigned long lastStatusPublish;
    TimeManager* timeManagerRef;
    Seismograph* seismographRef;
    bool debugModeEnabled;
    
    // Private methods
    void onMessageReceived(char* topic, byte* payload, unsigned int length);
    static void staticMessageCallback(char* topic, byte* payload, unsigned int length);
    bool reconnect();
    void processCommand(const String& command, const String& payload);
    void sendHeartbeat();

public:
    MQTTHandler();
    bool begin();
    void loop();
    bool isConnected();
    
    // Publishing methods
    bool publishData(const String& data);
    bool publishEvent(const String& event);
    bool publishSeismicEvent(const struct SeismicEventData& eventData);
    bool publishStatus(const String& status);
    bool publish(const String& topic, const String& payload, bool retained = false);
    
    // Subscription methods
    bool subscribe(const String& topic);
    bool unsubscribe(const String& topic);
    
    // Utility methods
    void setLastWillTestament();
    String createDataJson(float accelX, float accelY, float accelZ, float magnitude);
    String createEventJson(const String& eventType, float magnitude, int level);
    
    // Scheduled publishing methods
    bool publishDataSummary(const String& summary);
    bool publishStatusUpdate(const String& status);
    void checkScheduledPublishing();
    
    // Reference management
    void setTimeManagerReference(TimeManager* timeManager);
    void setSeismographReference(Seismograph* seismograph);
    
    // Debug mode management
    bool isDebugModeEnabled() { return debugModeEnabled; }
    void setDebugMode(bool enabled) { debugModeEnabled = enabled; }
};

// Global instance for callback
extern MQTTHandler* globalMQTTHandler;

#endif // MQTT_HANDLER_H
