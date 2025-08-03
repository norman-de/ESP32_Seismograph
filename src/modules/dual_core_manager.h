#ifndef DUAL_CORE_MANAGER_H
#define DUAL_CORE_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "config.h"

// Forward declarations
class Seismograph;
class DataLogger;
class MQTTHandler;
class WebServerManager;

struct SensorDataPacket {
    float accelX;
    float accelY;
    float accelZ;
    float magnitude;
    unsigned long timestamp;
};

struct EventPacket {
    String eventType;
    float magnitude;
    int level;
    unsigned long timestamp;
};

class DualCoreManager {
public:
    bool detailedLoggingEnabled;
private:
    // Task handles
    TaskHandle_t sensorTaskHandle;
    TaskHandle_t backgroundTaskHandle;
    
    // Queues for inter-core communication
    QueueHandle_t sensorDataQueue;
    QueueHandle_t eventQueue;
    
    // Task statistics
    unsigned long sensorTaskCount;
    unsigned long backgroundTaskCount;
    unsigned long lastStatsUpdate;
    
    // References to other modules
    Seismograph* seismographRef;
    DataLogger* dataLoggerRef;
    MQTTHandler* mqttHandlerRef;
    WebServerManager* webServerRef;
    
    bool initialized;
    
    // Static task functions
    static void sensorTask(void* parameter);
    static void backgroundTask(void* parameter);
    
    // Instance methods called by static functions
    void runSensorTask();
    void runBackgroundTask();

public:
    DualCoreManager();
    bool begin();
    void setReferences(Seismograph* seismo, DataLogger* logger, MQTTHandler* mqtt);
    void setWebServerReference(WebServerManager* webServer);
    
    // Queue operations
    bool sendSensorData(const SensorDataPacket& data);
    bool sendEvent(const EventPacket& event);
    bool receiveSensorData(SensorDataPacket& data, TickType_t timeout = 0);
    bool receiveEvent(EventPacket& event, TickType_t timeout = 0);
    
    // Statistics
    void printStats();
    unsigned long getSensorTaskCount() { return sensorTaskCount; }
    unsigned long getBackgroundTaskCount() { return backgroundTaskCount; }
    
    // Task management
    void suspendSensorTask();
    void resumeSensorTask();
    void suspendBackgroundTask();
    void resumeBackgroundTask();
};

// Global instance for task access
extern DualCoreManager* globalCoreManager;

#endif // DUAL_CORE_MANAGER_H
