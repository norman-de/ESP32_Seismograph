#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include "config.h"

// Module includes
#include "modules/dual_core_manager.h"
#include "modules/seismograph.h"
#include "modules/data_logger.h"
#include "modules/mqtt_handler.h"
#include "modules/web_server.h"
#include "modules/time_manager.h"
#include "utils/led_controller.h"

// Global objects
DualCoreManager coreManager;
Seismograph seismograph;
DataLogger dataLogger;
MQTTHandler mqttHandler;
WebServerManager webServer;
TimeManager timeManager;
LEDController ledController;

// Global references for modules
DataLogger* globalDataLogger = &dataLogger;
// Global variables
bool systemInitialized = false;
unsigned long lastHealthCheck = 0;
unsigned long lastPerformanceLog = 0;
bool detailedLoggingEnabled = false; // Global flag for detailed logging

// Function declarations
void setupOTA();
void performHealthCheck();
void logPerformanceStats();
void updateStatusLED();
String createStatusJson();

void toggleDetailedLogging(AsyncWebServerRequest *request);

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32 Seismograph Starting ===");
    
    // Configure Task Watchdog Timer
    Serial.println("Configuring Task Watchdog Timer...");
    esp_task_wdt_init(TASK_WATCHDOG_TIMEOUT_S, true); // 30 second timeout, panic on timeout
    esp_task_wdt_add(NULL); // Add current task (setup/loop) to watchdog
    Serial.printf("Task Watchdog configured: %d seconds timeout\n", TASK_WATCHDOG_TIMEOUT_S);
    
    // Initialize LED first for status indication
    ledController.begin();
    ledController.setColor(0, 0, 255); // Blue - Initializing
    
    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS Mount Failed");
        ledController.setColor(255, 0, 0); // Red - Error
        while(1) delay(1000);
    }
    if (detailedLoggingEnabled) Serial.println("LittleFS initialized");
    
    // Initialize I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (detailedLoggingEnabled) Serial.println("I2C initialized");
    
    // Initialize data logger
    if (!dataLogger.begin()) {
        Serial.println("ERROR: Data Logger initialization failed");
        ledController.setColor(255, 0, 0); // Red - Error
        while(1) delay(1000);
    }
    dataLogger.setDetailedLogging(detailedLoggingEnabled);
    if (detailedLoggingEnabled) Serial.println("Data logger initialized");
    
    // Initialize seismograph
    if (!seismograph.begin()) {
        Serial.println("ERROR: Seismograph initialization failed");
        ledController.setColor(255, 0, 0); // Red - Error
        while(1) delay(1000);
    }
    seismograph.detailedLoggingEnabled = detailedLoggingEnabled;
    if (detailedLoggingEnabled) Serial.println("Seismograph initialized");
    
    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    Serial.print("Connecting to WiFi");
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
        delay(1000);
        Serial.print(".");
        wifiAttempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("WiFi connected! IP: ");
        Serial.println(WiFi.localIP());
        
        // Initialize time manager
        timeManager.begin();
        if (detailedLoggingEnabled) Serial.println("Time manager initialized");
        
        // Initialize MQTT
        mqttHandler.begin();
        if (detailedLoggingEnabled) Serial.println("MQTT handler initialized");
        
        // Set TimeManager reference for MQTT handler
        mqttHandler.setTimeManagerReference(&timeManager);
        if (detailedLoggingEnabled) Serial.println("MQTT handler TimeManager reference set");
        
        // Set Seismograph reference for MQTT handler
        mqttHandler.setSeismographReference(&seismograph);
        if (detailedLoggingEnabled) Serial.println("MQTT handler Seismograph reference set");
        
        // Set MQTT reference for DataLogger (for automatic seismic event publishing)
        dataLogger.setMQTTReference(&mqttHandler);
        if (detailedLoggingEnabled) Serial.println("DataLogger MQTT reference set");
        
        // Initialize web server
        webServer.begin();
        if (detailedLoggingEnabled) Serial.println("Web server initialized");
        
        // Set references for web server
        webServer.setReferences(&seismograph, &dataLogger, &mqttHandler, &timeManager);
        webServer.addHttpEndpoint("/toggle_logging", HTTP_GET, [](AsyncWebServerRequest *request){
            toggleDetailedLogging(request);
        });
        if (detailedLoggingEnabled) Serial.println("Web server references set");
        
        // Initialize OTA
        setupOTA();
        if (detailedLoggingEnabled) Serial.println("OTA initialized");
        
        ledController.setColor(0, 255, 255); // Cyan - WiFi connected
    } else {
        Serial.println("\nWiFi connection failed - running in offline mode");
        ledController.setColor(255, 255, 0); // Yellow - Warning
    }
    
    // Set references for dual core manager
    coreManager.setReferences(&seismograph, &dataLogger, &mqttHandler);
    coreManager.setWebServerReference(&webServer);
    
    // Initialize dual core manager (must be last)
    if (!coreManager.begin()) {
        Serial.println("ERROR: Dual Core Manager initialization failed");
        ledController.setColor(255, 0, 0); // Red - Error
        while(1) delay(1000);
    }
    if (detailedLoggingEnabled) Serial.println("Dual core manager initialized");
    
    // System ready
    systemInitialized = true;
    ledController.setColor(0, 255, 0); // Green - Ready
    
    Serial.println("=== System Ready ===");
    if (detailedLoggingEnabled) {
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("CPU frequency: %d MHz\n", ESP.getCpuFreqMHz());
    }
    
    // Log system start
    dataLogger.logEvent("SYSTEM_START", "System initialized successfully", 0);
}

void loop() {
    unsigned long currentTime = millis();
    
    // Reset watchdog timer to prevent timeout
    esp_task_wdt_reset();
    
    // Health check
    if (currentTime - lastHealthCheck >= HEALTH_CHECK_INTERVAL) {
        performHealthCheck();
        lastHealthCheck = currentTime;
        esp_task_wdt_reset(); // Reset after potentially long operation
    }
    
    // Performance logging
    if (currentTime - lastPerformanceLog >= PERFORMANCE_LOG_INTERVAL) {
        logPerformanceStats();
        lastPerformanceLog = currentTime;
        esp_task_wdt_reset(); // Reset after potentially long operation
    }
    
    // Update components
    ledController.update(); // Update LED blinking
    
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
        esp_task_wdt_reset(); // Reset after OTA handling
        
        mqttHandler.loop();
        esp_task_wdt_reset(); // Reset after MQTT operations
        
        timeManager.loop();
        esp_task_wdt_reset(); // Reset after time manager operations
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}

void performHealthCheck() {
    uint32_t freeHeap = ESP.getFreeHeap();
    
    // Check memory
    if (freeHeap < MIN_FREE_HEAP) {
        Serial.printf("WARNING: Low memory! Free heap: %d bytes\n", freeHeap);
        dataLogger.logEvent("LOW_MEMORY", "Low memory warning", freeHeap);
    }
    
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WARNING: WiFi disconnected");
        // Try to reconnect
        WiFi.reconnect();
    }
    
    // Check MQTT connection
    if (WiFi.status() == WL_CONNECTED && !mqttHandler.isConnected()) {
        Serial.println("WARNING: MQTT disconnected");
    }
    
    // Update status LED
    updateStatusLED();
    
    // Send status via MQTT if connected (using scheduled intervals)
    if (mqttHandler.isConnected()) {
        String statusJson = createStatusJson();
        mqttHandler.publishStatusUpdate(statusJson);
    }
}

void logPerformanceStats() {
    if (!detailedLoggingEnabled) return;

    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minFreeHeap = ESP.getMinFreeHeap();
    uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
    
    Serial.println("=== Performance Stats ===");
    Serial.printf("Free heap: %d bytes\n", freeHeap);
    Serial.printf("Min free heap: %d bytes\n", minFreeHeap);
    Serial.printf("Max alloc heap: %d bytes\n", maxAllocHeap);
    Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
    
    // Log core statistics
    coreManager.printStats();
    
    // Log sensor statistics
    seismograph.printStats();
    
    dataLogger.logEvent("PERFORMANCE", "Performance statistics logged", freeHeap);
}

void updateStatusLED() {
    if (!systemInitialized) {
        ledController.setColor(0, 0, 255); // Blue - Initializing
        return;
    }
    
    // Check for critical errors first
    if (ESP.getFreeHeap() < MIN_FREE_HEAP / 2) {
        ledController.setColor(255, 0, 0); // Red - Critical error
        return;
    }
    
    // Check MQTT connection
    if (WiFi.status() == WL_CONNECTED) {
        if (mqttHandler.isConnected()) {
            ledController.setColor(0, 255, 0); // Green - All good
        } else {
            ledController.setColor(128, 0, 128); // Purple - MQTT disconnected
        }
    } else {
        ledController.setColor(255, 255, 0); // Yellow - WiFi disconnected
    }
}

String createStatusJson() {
    // Pre-allocate string buffer for better performance
    String json;
    json.reserve(512);
    
    // Use sprintf for more efficient string building
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
        "{"
        "\"uptime\":%lu,"
        "\"free_heap\":%u,"
        "\"min_free_heap\":%u,"
        "\"wifi_connected\":%s,"
        "\"mqtt_connected\":%s,"
        "\"ip_address\":\"%s\","
        "\"rssi\":%d,"
        "\"timestamp\":%lu,"
        "\"sensor_calibrated\":%s,"
        "\"events_detected\":%lu,"
        "\"last_magnitude\":%.4f,"
        "\"ota_enabled\":true"
        "}",
        millis() / 1000,
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        WiFi.status() == WL_CONNECTED ? "true" : "false",
        mqttHandler.isConnected() ? "true" : "false",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        timeManager.isTimeValid() ? timeManager.getEpochTime() : 0,
        seismograph.isCalibrated() ? "true" : "false",
        seismograph.getEventsDetected(),
        seismograph.getLastMagnitude()
    );
    
    json = buffer;
    return json;
}

void toggleDetailedLogging(AsyncWebServerRequest *request) {
    detailedLoggingEnabled = !detailedLoggingEnabled;
    seismograph.detailedLoggingEnabled = detailedLoggingEnabled;
    dataLogger.setDetailedLogging(detailedLoggingEnabled);
    String message = "Detailed logging " + String(detailedLoggingEnabled ? "enabled" : "disabled");
    webServer.send(request, 200, "text/plain", message);
    Serial.println(message);
}

void setupOTA() {
    // Configure OTA hostname and authentication
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setPort(OTA_PORT);
    
    // Configure OTA callbacks
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_SPIFFS
            type = "filesystem";
        }
        
        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
        
        // Log OTA start
        dataLogger.logEvent("OTA_START", "OTA update started: " + type, 0);
        
        // Set LED to indicate OTA update
        ledController.setColor(255, 165, 0); // Orange - OTA in progress
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA Update completed successfully");
        dataLogger.logEvent("OTA_SUCCESS", "OTA update completed successfully", 0);
        
        // Set LED to indicate success
        ledController.setColor(0, 255, 0); // Green - Success
        delay(1000);
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
        
        // Blink LED during progress
        static unsigned long lastBlink = 0;
        if (millis() - lastBlink > 200) {
            static bool ledState = false;
            ledController.setColor(ledState ? 255 : 0, ledState ? 165 : 0, 0); // Blink orange
            ledState = !ledState;
            lastBlink = millis();
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        String errorMsg = "Unknown error";
        
        if (error == OTA_AUTH_ERROR) {
            errorMsg = "Auth Failed";
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            errorMsg = "Begin Failed";
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            errorMsg = "Connect Failed";
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            errorMsg = "Receive Failed";
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            errorMsg = "End Failed";
            Serial.println("End Failed");
        }
        
        // Log OTA error
        dataLogger.logEvent("OTA_ERROR", "OTA update failed: " + errorMsg, error);
        
        // Set LED to indicate error
        ledController.setColor(255, 0, 0); // Red - Error
    });
    
    // Start OTA service
    ArduinoOTA.begin();
    
    Serial.printf("OTA Ready! Hostname: %s, Port: %d\n", OTA_HOSTNAME, OTA_PORT);
    Serial.printf("OTA IP address: %s\n", WiFi.localIP().toString().c_str());
}
