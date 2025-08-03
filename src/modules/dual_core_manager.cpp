#include "dual_core_manager.h"
#include "seismograph.h"
#include "data_logger.h"
#include "mqtt_handler.h"
#include "web_server.h"

// Global instance for task access
DualCoreManager* globalCoreManager = nullptr;

DualCoreManager::DualCoreManager() {
    detailedLoggingEnabled = false;
    sensorTaskHandle = nullptr;
    backgroundTaskHandle = nullptr;
    sensorDataQueue = nullptr;
    eventQueue = nullptr;
    
    sensorTaskCount = 0;
    backgroundTaskCount = 0;
    lastStatsUpdate = 0;
    
    seismographRef = nullptr;
    dataLoggerRef = nullptr;
    mqttHandlerRef = nullptr;
    webServerRef = nullptr;
    
    initialized = false;
    globalCoreManager = this;
}

bool DualCoreManager::begin() {
    if (detailedLoggingEnabled) Serial.println("Initializing Dual Core Manager...");
    
    // Create queues for inter-core communication
    sensorDataQueue = xQueueCreate(SENSOR_DATA_QUEUE_SIZE, sizeof(SensorDataPacket));
    if (sensorDataQueue == nullptr) {
        Serial.println("ERROR: Failed to create sensor data queue");
        return false;
    }
    
    eventQueue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(EventPacket));
    if (eventQueue == nullptr) {
        Serial.println("ERROR: Failed to create event queue");
        vQueueDelete(sensorDataQueue);
        return false;
    }
    
    // Create sensor task on Core 0 (high priority)
    BaseType_t result = xTaskCreatePinnedToCore(
        sensorTask,                 // Task function
        "SensorTask",               // Task name
        SENSOR_TASK_STACK_SIZE,     // Stack size
        this,                       // Parameter
        SENSOR_TASK_PRIORITY,       // Priority
        &sensorTaskHandle,          // Task handle
        0                           // Core 0
    );
    
    if (result != pdPASS) {
        Serial.println("ERROR: Failed to create sensor task");
        vQueueDelete(sensorDataQueue);
        vQueueDelete(eventQueue);
        return false;
    }
    
    // Create background task on Core 1 (lower priority)
    result = xTaskCreatePinnedToCore(
        backgroundTask,             // Task function
        "BackgroundTask",           // Task name
        BACKGROUND_TASK_STACK_SIZE, // Stack size
        this,                       // Parameter
        BACKGROUND_TASK_PRIORITY,   // Priority
        &backgroundTaskHandle,      // Task handle
        1                           // Core 1
    );
    
    if (result != pdPASS) {
        Serial.println("ERROR: Failed to create background task");
        vTaskDelete(sensorTaskHandle);
        vQueueDelete(sensorDataQueue);
        vQueueDelete(eventQueue);
        return false;
    }
    
    initialized = true;
    if (detailedLoggingEnabled) {
        Serial.println("Dual Core Manager initialized successfully");
        Serial.printf("Sensor task running on Core 0, priority %d\n", SENSOR_TASK_PRIORITY);
        Serial.printf("Background task running on Core 1, priority %d\n", BACKGROUND_TASK_PRIORITY);
    }
    
    return true;
}

void DualCoreManager::setReferences(Seismograph* seismo, DataLogger* logger, MQTTHandler* mqtt) {
    seismographRef = seismo;
    dataLoggerRef = logger;
    mqttHandlerRef = mqtt;
}

void DualCoreManager::setWebServerReference(WebServerManager* webServer) {
    webServerRef = webServer;
}

void DualCoreManager::sensorTask(void* parameter) {
    DualCoreManager* manager = static_cast<DualCoreManager*>(parameter);
    manager->runSensorTask();
}

void DualCoreManager::backgroundTask(void* parameter) {
    DualCoreManager* manager = static_cast<DualCoreManager*>(parameter);
    manager->runBackgroundTask();
}

void DualCoreManager::runSensorTask() {
    if (detailedLoggingEnabled) Serial.println("Sensor task started on Core 0");
    
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(SAMPLING_INTERVAL);
    
    while (true) {
        sensorTaskCount++;
        
        // Read sensor data if seismograph is available
        if (seismographRef != nullptr) {
            SensorData data = seismographRef->readSensor();
            
            // Process the data
            seismographRef->processData(data);
            
            // Send data to background task via queue
            SensorDataPacket packet;
            packet.accelX = data.accelX;
            packet.accelY = data.accelY;
            packet.accelZ = data.accelZ;
            packet.magnitude = data.magnitude;
            packet.timestamp = data.timestamp;
            
            sendSensorData(packet);
        }
        
        // Wait for next sampling interval
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

void DualCoreManager::runBackgroundTask() {
    if (detailedLoggingEnabled) Serial.println("Background task started on Core 1");
    
    SensorDataPacket sensorData;
    EventPacket eventData;
    
    while (true) {
        backgroundTaskCount++;
        
        // Process sensor data from queue
        if (receiveSensorData(sensorData, pdMS_TO_TICKS(10))) {
            // Log sensor data if data logger is available
            if (dataLoggerRef != nullptr) {
                dataLoggerRef->logSensorData(sensorData.accelX, sensorData.accelY, 
                                           sensorData.accelZ, sensorData.magnitude);
            }
            
            // Send data via MQTT if handler is available (using scheduled intervals)
            if (mqttHandlerRef != nullptr && mqttHandlerRef->isConnected()) {
                String dataJson = mqttHandlerRef->createDataJson(sensorData.accelX, sensorData.accelY,
                                                                sensorData.accelZ, sensorData.magnitude);
                mqttHandlerRef->publishDataSummary(dataJson);
            }
            
            // Update WebSocket clients with real-time sensor data
            if (webServerRef != nullptr) {
                webServerRef->updateSensorData(sensorData.accelX, sensorData.accelY, 
                                             sensorData.accelZ, sensorData.magnitude);
            }
        }
        
        // Process events from queue
        if (receiveEvent(eventData, pdMS_TO_TICKS(10))) {
            // Log event if data logger is available
            if (dataLoggerRef != nullptr) {
                dataLoggerRef->logEvent(eventData.eventType, "Seismic event detected", eventData.magnitude);
            }
            
            // Send event via MQTT if handler is available
            if (mqttHandlerRef != nullptr && mqttHandlerRef->isConnected()) {
                String eventJson = mqttHandlerRef->createEventJson(eventData.eventType, 
                                                                 eventData.magnitude, eventData.level);
                mqttHandlerRef->publishEvent(eventJson);
            }
            
            // Send seismic event to WebSocket clients
            if (webServerRef != nullptr) {
                webServerRef->sendSeismicEvent(eventData.eventType, eventData.magnitude, eventData.level);
            }
        }
        
        // Small delay to prevent watchdog issues
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

bool DualCoreManager::sendSensorData(const SensorDataPacket& data) {
    if (sensorDataQueue == nullptr) return false;
    
    return xQueueSend(sensorDataQueue, &data, 0) == pdTRUE;
}

bool DualCoreManager::sendEvent(const EventPacket& event) {
    if (eventQueue == nullptr) return false;
    
    return xQueueSend(eventQueue, &event, 0) == pdTRUE;
}

bool DualCoreManager::receiveSensorData(SensorDataPacket& data, TickType_t timeout) {
    if (sensorDataQueue == nullptr) return false;
    
    return xQueueReceive(sensorDataQueue, &data, timeout) == pdTRUE;
}

bool DualCoreManager::receiveEvent(EventPacket& event, TickType_t timeout) {
    if (eventQueue == nullptr) return false;
    
    return xQueueReceive(eventQueue, &event, timeout) == pdTRUE;
}

void DualCoreManager::printStats() {
    if (!detailedLoggingEnabled) return;

    unsigned long currentTime = millis();
    
    if (currentTime - lastStatsUpdate >= 5000) { // Update every 5 seconds
        lastStatsUpdate = currentTime;
        
        Serial.println("=== Dual Core Manager Statistics ===");
        Serial.printf("Sensor task count: %lu\n", sensorTaskCount);
        Serial.printf("Background task count: %lu\n", backgroundTaskCount);
        
        if (sensorTaskCount > 0) {
            float sensorRate = (float)sensorTaskCount / (currentTime / 1000.0f);
            Serial.printf("Sensor task rate: %.2f Hz\n", sensorRate);
        }
        
        if (backgroundTaskCount > 0) {
            float backgroundRate = (float)backgroundTaskCount / (currentTime / 1000.0f);
            Serial.printf("Background task rate: %.2f Hz\n", backgroundRate);
        }
        
        // Queue statistics
        if (sensorDataQueue != nullptr) {
            UBaseType_t sensorQueueWaiting = uxQueueMessagesWaiting(sensorDataQueue);
            UBaseType_t sensorQueueSpaces = uxQueueSpacesAvailable(sensorDataQueue);
            Serial.printf("Sensor queue: %d waiting, %d free\n", sensorQueueWaiting, sensorQueueSpaces);
        }
        
        if (eventQueue != nullptr) {
            UBaseType_t eventQueueWaiting = uxQueueMessagesWaiting(eventQueue);
            UBaseType_t eventQueueSpaces = uxQueueSpacesAvailable(eventQueue);
            Serial.printf("Event queue: %d waiting, %d free\n", eventQueueWaiting, eventQueueSpaces);
        }
        
        // Task stack usage
        if (sensorTaskHandle != nullptr) {
            UBaseType_t sensorStackHighWater = uxTaskGetStackHighWaterMark(sensorTaskHandle);
            Serial.printf("Sensor task stack high water mark: %d bytes\n", sensorStackHighWater * sizeof(StackType_t));
        }
        
        if (backgroundTaskHandle != nullptr) {
            UBaseType_t backgroundStackHighWater = uxTaskGetStackHighWaterMark(backgroundTaskHandle);
            Serial.printf("Background task stack high water mark: %d bytes\n", backgroundStackHighWater * sizeof(StackType_t));
        }
    }
}

void DualCoreManager::suspendSensorTask() {
    if (sensorTaskHandle != nullptr) {
        vTaskSuspend(sensorTaskHandle);
        if (detailedLoggingEnabled) Serial.println("Sensor task suspended");
    }
}

void DualCoreManager::resumeSensorTask() {
    if (sensorTaskHandle != nullptr) {
        vTaskResume(sensorTaskHandle);
        if (detailedLoggingEnabled) Serial.println("Sensor task resumed");
    }
}

void DualCoreManager::suspendBackgroundTask() {
    if (backgroundTaskHandle != nullptr) {
        vTaskSuspend(backgroundTaskHandle);
        if (detailedLoggingEnabled) Serial.println("Background task suspended");
    }
}

void DualCoreManager::resumeBackgroundTask() {
    if (backgroundTaskHandle != nullptr) {
        vTaskResume(backgroundTaskHandle);
        if (detailedLoggingEnabled) Serial.println("Background task resumed");
    }
}
