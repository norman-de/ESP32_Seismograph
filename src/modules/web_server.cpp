#include "web_server.h"
#include "seismograph.h"
#include "data_logger.h"
#include "mqtt_handler.h"
#include "time_manager.h"

WebServerManager::WebServerManager() : server(WEB_SERVER_PORT), ws("/ws") {
    initialized = false;
    seismographRef = nullptr;
    dataLoggerRef = nullptr;
    mqttHandlerRef = nullptr;
    timeManagerRef = nullptr;
    
    // Initialize WebSocket variables
    lastSensorBroadcast = 0;
    lastStatusBroadcast = 0;
    realtimeStreamingEnabled = true;
}

bool WebServerManager::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: WiFi not connected, cannot start web server");
        return false;
    }
    
    // Setup routes
    setupRoutes();
    
    // Start server
    server.begin();
    initialized = true;
    
    Serial.printf("Web server started on http://%s:%d\n", 
                  WiFi.localIP().toString().c_str(), WEB_SERVER_PORT);
    
    return true;
}

void WebServerManager::loop() {
    // AsyncWebServer handles requests automatically
    // No explicit loop needed
}

void WebServerManager::setReferences(Seismograph* seismo, DataLogger* logger, MQTTHandler* mqtt, TimeManager* time) {
    seismographRef = seismo;
    dataLoggerRef = logger;
    mqttHandlerRef = mqtt;
    timeManagerRef = time;
}

void WebServerManager::addHttpEndpoint(const char* uri, WebRequestMethodComposite method, std::function<void(AsyncWebServerRequest *request)> onRequest) {
    server.on(uri, method, onRequest);
}

void WebServerManager::send(AsyncWebServerRequest *request, int code, const char* contentType, const String& content) {
    request->send(code, contentType, content);
}

void WebServerManager::setupRoutes() {
    // Setup WebSocket
    ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        onWebSocketEvent(server, client, type, arg, data, len);
    });
    server.addHandler(&ws);
    
    // API endpoints MUST be defined BEFORE static file serving
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleStatus(request);
    });
    
    server.on("/api/data", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleData(request);
    });
    
    server.on("/api/seismic-events", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleSeismicEvents(request);
    });
    
    
    server.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleRestart(request);
    });
    
    server.on("/api/simulate", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSimulate(request);
    });
    
    // Serve static files from LittleFS (AFTER API endpoints)
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    
    // 404 handler for API endpoints only
    server.onNotFound([](AsyncWebServerRequest *request) {
        String path = request->url();
        if (path.startsWith("/api/")) {
            request->send(404, "application/json", "{\"error\":\"API endpoint not found\"}");
        } else {
            // For non-API requests, try to serve from filesystem or return 404
            request->send(404, "text/html", 
                "<html><body><h1>404 - Page Not Found</h1>"
                "<p>The requested page could not be found.</p>"
                "<a href='/'>Return to Home</a></body></html>");
        }
    });
}


void WebServerManager::handleStatus(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["ip_address"] = WiFi.localIP().toString();
    doc["mqtt_connected"] = (mqttHandlerRef != nullptr) ? mqttHandlerRef->isConnected() : false;
    
    // Add seismograph status if available
    if (seismographRef != nullptr) {
        doc["sensor_calibrated"] = seismographRef->isCalibrated();
        doc["events_detected"] = seismographRef->getEventsDetected();
        doc["last_magnitude"] = seismographRef->getLastMagnitude();
    }
    
    // Add time information if available
    if (timeManagerRef != nullptr) {
        doc["time_valid"] = timeManagerRef->isTimeValid();
        if (timeManagerRef->isTimeValid()) {
            doc["timestamp"] = timeManagerRef->getEpochTime();
        }
    }
    
    // Add OTA information
    doc["ota_enabled"] = true;
    doc["ota_hostname"] = OTA_HOSTNAME;
    doc["ota_port"] = OTA_PORT;
    
    String response;
    serializeJson(doc, response);
    
    request->send(200, "application/json", response);
}

void WebServerManager::handleData(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["timestamp"] = millis();
    
    if (seismographRef != nullptr) {
        SensorData data = seismographRef->readSensor();
        doc["accel_x"] = data.accelX;
        doc["accel_y"] = data.accelY;
        doc["accel_z"] = data.accelZ;
        doc["magnitude"] = data.magnitude;
        doc["sensor_timestamp"] = data.timestamp;
        doc["calibrated"] = seismographRef->isCalibrated();
        doc["events_detected"] = seismographRef->getEventsDetected();
    } else {
        doc["error"] = "Seismograph not available";
    }
    
    String response;
    serializeJson(doc, response);
    
    request->send(200, "application/json", response);
}

void WebServerManager::handleSeismicEvents(AsyncWebServerRequest *request) {
    if (dataLoggerRef != nullptr) {
        // Limit events to prevent blocking and add timeout protection
        int maxEvents = 25; // Reduced from 100 to prevent blocking
        if (request->hasParam("limit")) {
            int requestedLimit = request->getParam("limit")->value().toInt();
            maxEvents = constrain(requestedLimit, 1, 50); // Max 50 events
        }
        
        Serial.printf("Seismic events requested via API (limit: %d)\n", maxEvents);
        
        // Get events with limited count to prevent blocking
        String seismicEventsJson = dataLoggerRef->getFullSeismicEventsJson(maxEvents);
        
        // Check if JSON is too large (prevent memory issues)
        if (seismicEventsJson.length() > 32768) { // 32KB limit
            Serial.printf("WARNING: Large JSON response (%d bytes), truncating...\n", seismicEventsJson.length());
            
            // Fallback to smaller dataset
            seismicEventsJson = dataLoggerRef->getFullSeismicEventsJson(10);
            
            if (seismicEventsJson.length() > 32768) {
                // Still too large, send error
                JsonDocument doc;
                doc["error"] = "Response too large";
                doc["message"] = "Too many events, please use limit parameter";
                doc["max_recommended_limit"] = 10;
                
                String response;
                serializeJson(doc, response);
                request->send(413, "application/json", response); // 413 Payload Too Large
                return;
            }
        }
        
        request->send(200, "application/json", seismicEventsJson);
        
        if (dataLoggerRef->detailedLoggingEnabled) {
            Serial.printf("Seismic events data sent: %d bytes\n", seismicEventsJson.length());
        }
    } else {
        // Fallback response if data logger not available
        JsonDocument doc;
        doc["events"] = JsonArray();
        doc["total_count"] = 0;
        doc["message"] = "Data logger not available";
        
        String response;
        serializeJson(doc, response);
        request->send(500, "application/json", response);
    }
}


void WebServerManager::handleSimulate(AsyncWebServerRequest *request) {
    if (seismographRef == nullptr) {
        request->send(500, "text/plain", "Seismograph not available");
        return;
    }
    
    // Default values for micro event
    float targetRichter = 1.5f;
    
    // Get Richter scale value from request
    if (request->hasParam("richter")) {
        targetRichter = request->getParam("richter")->value().toFloat();
        // Validate Richter scale range (0.0 to 9.0)
        targetRichter = constrain(targetRichter, 0.0f, 9.0f);
    } else if (request->hasParam("magnitude")) {
        // Direct magnitude input (for API compatibility)
        float directMagnitude = request->getParam("magnitude")->value().toFloat();
        targetRichter = seismographRef->calculateRichterMagnitude(directMagnitude);
    }
    
    // Calculate magnitude from Richter scale using scientific formula
    float magnitude = pow(10, (targetRichter + 2.0) / 3.0);
    
    // Determine event type from Richter scale
    String type = seismographRef->getEventTypeFromRichter(targetRichter);
    
    // Simulate realistic event duration based on magnitude
    unsigned long simulatedDuration = 500 + (magnitude * 15000);
    simulatedDuration = constrain(simulatedDuration, 500, 3000);
    
    // Create scientific description
    String scientificDescription = seismographRef->getScientificEventDescription(magnitude, simulatedDuration);
    
    // Simulate the event in seismograph
    Serial.printf("Simulating %s seismic event via web interface (%.4f g, Richter %.2f)\n", 
                  type.c_str(), magnitude, targetRichter);
    seismographRef->simulateEvent(magnitude);
    
    // Log the simulation with scientific data
    if (dataLoggerRef != nullptr) {
        String description = "Web simulation: " + type + " event | " + scientificDescription;
        dataLoggerRef->logEvent(type, description, magnitude);
    }
    
    String response = "Simulated " + type + " seismic event (Richter " + String(targetRichter, 2) + 
                     ", " + String(magnitude, 4) + "g)";
    request->send(200, "text/plain", response);
}

void WebServerManager::handleRestart(AsyncWebServerRequest *request) {
    Serial.println("Restart requested via web interface");
    if (dataLoggerRef != nullptr) {
        dataLoggerRef->logEvent("WEB_RESTART", "System restart via web interface", 0);
    }
    
    request->send(200, "text/plain", "System restarting...");
    
    // Restart after a short delay
    delay(1000);
    ESP.restart();
}


void WebServerManager::sendEventToClients(const String& event) {
    broadcastEvent("system", event);
}

void WebServerManager::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch(type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            // Send initial status to new client
            client->text("{\"type\":\"status\",\"message\":\"Connected to seismograph\",\"clients\":" + String(ws.count()) + "}");
            break;
            
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
            
        case WS_EVT_DATA:
            handleWebSocketMessage(client, data, len);
            break;
            
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebServerManager::handleWebSocketMessage(AsyncWebSocketClient *client, uint8_t *data, size_t len) {
    String message = "";
    for(size_t i = 0; i < len; i++) {
        message += (char)data[i];
    }
    
    Serial.printf("WebSocket message from client #%u: %s\n", client->id(), message.c_str());
    
    // Parse JSON message
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        client->text("{\"type\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }
    
    String command = doc["command"] | "";
    
    if (command == "start_streaming") {
        realtimeStreamingEnabled = true;
        client->text("{\"type\":\"response\",\"message\":\"Real-time streaming started\"}");
        Serial.println("Real-time streaming enabled via WebSocket");
    }
    else if (command == "stop_streaming") {
        realtimeStreamingEnabled = false;
        client->text("{\"type\":\"response\",\"message\":\"Real-time streaming stopped\"}");
        Serial.println("Real-time streaming disabled via WebSocket");
    }
    else if (command == "get_status") {
        broadcastStatus();
    }
    else {
        client->text("{\"type\":\"error\",\"message\":\"Unknown command: " + command + "\"}");
    }
}

void WebServerManager::broadcastSensorData() {
    if (!realtimeStreamingEnabled || ws.count() == 0) {
        return;
    }
    
    // Get averaged data from buffer
    float avgX, avgY, avgZ, avgMag, maxMag;
    if (!sensorBuffer.getAveragedData(avgX, avgY, avgZ, avgMag, maxMag)) {
        return; // No data available
    }
    
    unsigned long now = millis();
    if (now - lastSensorBroadcast < 100) { // Reduced to 10 Hz max
        return;
    }
    
    JsonDocument doc;
    doc["type"] = "sensor_data";
    doc["timestamp"] = now;
    doc["accel_x"] = avgX;
    doc["accel_y"] = avgY;
    doc["accel_z"] = avgZ;
    doc["magnitude"] = avgMag;
    doc["max_magnitude"] = maxMag; // Peak value in buffer
    doc["sensor_timestamp"] = sensorBuffer.lastUpdate;
    doc["samples_averaged"] = sensorBuffer.sampleCount;
    
    if (seismographRef != nullptr) {
        doc["calibrated"] = seismographRef->isCalibrated();
        doc["events_detected"] = seismographRef->getEventsDetected();
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    // Use safe send method
    for (auto client : ws.getClients()) {
        if (client->status() == WS_CONNECTED) {
            safeSendToClient(client, jsonString);
        }
    }
    
    lastSensorBroadcast = now;
}

void WebServerManager::broadcastStatus() {
    if (ws.count() == 0) return;
    
    unsigned long now = millis();
    if (now - lastStatusBroadcast < 1000) { // Limit to 1 Hz
        return;
    }
    
    JsonDocument doc;
    doc["type"] = "status";
    doc["timestamp"] = now;
    doc["uptime"] = now / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["connected_clients"] = ws.count();
    doc["streaming_enabled"] = realtimeStreamingEnabled;
    
    if (seismographRef != nullptr) {
        doc["sensor_calibrated"] = seismographRef->isCalibrated();
        doc["events_detected"] = seismographRef->getEventsDetected();
        doc["last_magnitude"] = seismographRef->getLastMagnitude();
    }
    
    if (mqttHandlerRef != nullptr) {
        doc["mqtt_connected"] = mqttHandlerRef->isConnected();
    }
    
    if (timeManagerRef != nullptr) {
        doc["time_valid"] = timeManagerRef->isTimeValid();
        if (timeManagerRef->isTimeValid()) {
            doc["ntp_timestamp"] = timeManagerRef->getEpochTime();
        }
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    ws.textAll(jsonString);
    lastStatusBroadcast = now;
}

void WebServerManager::broadcastEvent(const String& eventType, const String& data) {
    if (ws.count() == 0) return;
    
    JsonDocument doc;
    doc["type"] = "event";
    doc["event_type"] = eventType;
    doc["data"] = data;
    doc["timestamp"] = millis();
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    ws.textAll(jsonString);
}

void WebServerManager::updateSensorData(float accelX, float accelY, float accelZ, float magnitude) {
    // Add sample to buffer instead of immediate broadcasting
    sensorBuffer.addSample(accelX, accelY, accelZ, magnitude, millis());
    
    // Use managed broadcast system instead of immediate broadcasting
    managedBroadcast();
}

void WebServerManager::sendSeismicEvent(const String& eventType, float magnitude, int level) {
    if (ws.count() == 0) return;
    
    JsonDocument doc;
    doc["type"] = "seismic_event";
    doc["event_type"] = eventType;
    doc["magnitude"] = magnitude;
    doc["level"] = level;
    doc["timestamp"] = millis();
    
    if (timeManagerRef != nullptr && timeManagerRef->isTimeValid()) {
        doc["ntp_timestamp"] = timeManagerRef->getEpochTime();
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    ws.textAll(jsonString);
    
    Serial.printf("Seismic event broadcasted via WebSocket: %s (%.4f g)\n", eventType.c_str(), magnitude);
}

String WebServerManager::getContentType(String filename) {
    if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".json")) return "application/json";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    return "text/plain";
}

String WebServerManager::processor(const String& var) {
    // Template processor for dynamic content
    if (var == "HOSTNAME") return HOSTNAME;
    if (var == "VERSION") return "1.0.0";
    if (var == "UPTIME") return String(millis() / 1000);
    return String();
}

// Advanced WebSocket methods implementation
void WebServerManager::managedBroadcast() {
    static unsigned long lastManagedBroadcast = 0;
    unsigned long now = millis();
    
    // Adaptive rate control based on client count and system load
    int broadcastInterval = 100; // Base 10 Hz
    if (ws.count() > 3) broadcastInterval = 150; // 6.7 Hz for many clients
    if (ESP.getFreeHeap() < 50000) broadcastInterval = 200; // 5 Hz when low memory
    
    if (now - lastManagedBroadcast >= broadcastInterval) {
        broadcastSensorData();
        lastManagedBroadcast = now;
        
        // Cleanup disconnected clients periodically
        static unsigned long lastCleanup = 0;
        if (now - lastCleanup > 10000) { // Every 10 seconds
            cleanupDisconnectedClients();
            lastCleanup = now;
        }
        
        // Print queue stats periodically
        static unsigned long lastStatsLog = 0;
        if (now - lastStatsLog > 30000) { // Every 30 seconds
            printQueueStats();
            lastStatsLog = now;
        }
    }
}

bool WebServerManager::safeSendToClient(AsyncWebSocketClient* client, const String& message) {
    if (!client || client->status() != WS_CONNECTED) {
        return false;
    }
    
    // Check client rate limiting instead of queue depth
    if (!canSendToClient(client->id())) {
        return false; // Rate limited, skip this send
    }
    
    // Simple send with error handling
    try {
        client->text(message);
        queueStats.successfulSends++;
        queueStats.totalMessages++;
        
        // Update client last sent time
        for (auto& info : clientInfo) {
            if (info.clientId == client->id()) {
                info.lastSent = millis();
                break;
            }
        }
        
        return true;
    } catch (...) {
        queueStats.queueErrors++;
        Serial.printf("ERROR: Failed to send to client #%u\n", client->id());
        updateClientInfo(client->id());
        return false;
    }
}

void WebServerManager::updateClientInfo(uint32_t clientId) {
    // Find or create client info
    for (auto& info : clientInfo) {
        if (info.clientId == clientId) {
            info.queueErrors++;
            info.lastSent = millis();
            
            // Reduce rate for problematic clients
            if (info.queueErrors > 3) {
                info.preferredRate = max(5, info.preferredRate - 1);
                Serial.printf("Reduced rate for client #%u to %d Hz due to errors\n", 
                             clientId, info.preferredRate);
            }
            return;
        }
    }
    
    // Add new client info
    ClientStreamingInfo newInfo;
    newInfo.clientId = clientId;
    newInfo.lastSent = millis();
    newInfo.queueErrors = 1;
    clientInfo.push_back(newInfo);
}

void WebServerManager::cleanupDisconnectedClients() {
    // Remove info for disconnected clients
    auto it = clientInfo.begin();
    while (it != clientInfo.end()) {
        bool clientExists = false;
        for (auto client : ws.getClients()) {
            if (client->id() == it->clientId) {
                clientExists = true;
                break;
            }
        }
        
        if (!clientExists) {
            Serial.printf("Removing info for disconnected client #%u\n", it->clientId);
            it = clientInfo.erase(it);
        } else {
            ++it;
        }
    }
}

void WebServerManager::printQueueStats() {
    if (queueStats.totalMessages == 0) return;
    
    float errorRate = (float)queueStats.queueErrors / queueStats.totalMessages * 100.0f;
    
    Serial.println("=== WebSocket Queue Statistics ===");
    Serial.printf("Total messages: %d\n", queueStats.totalMessages);
    Serial.printf("Successful sends: %d\n", queueStats.successfulSends);
    Serial.printf("Queue errors: %d (%.1f%%)\n", queueStats.queueErrors, errorRate);
    Serial.printf("Connected clients: %d\n", ws.count());
    Serial.printf("Tracked clients: %d\n", clientInfo.size());
    
    // Reset stats periodically
    if (millis() - queueStats.lastReset > 300000) { // Every 5 minutes
        resetQueueStats();
    }
}

void WebServerManager::resetQueueStats() {
    queueStats.totalMessages = 0;
    queueStats.queueErrors = 0;
    queueStats.successfulSends = 0;
    queueStats.lastReset = millis();
    Serial.println("WebSocket queue statistics reset");
}

bool WebServerManager::canSendToClient(uint32_t clientId) {
    for (const auto& info : clientInfo) {
        if (info.clientId == clientId) {
            unsigned long now = millis();
            unsigned long interval = 1000 / info.preferredRate;
            return (now - info.lastSent) >= interval;
        }
    }
    return true; // New client, allow sending
}

void WebServerManager::adaptiveRateControl() {
    // Adjust global streaming rate based on system performance
    static unsigned long lastAdaptation = 0;
    unsigned long now = millis();
    
    if (now - lastAdaptation < 5000) return; // Check every 5 seconds
    
    uint32_t freeHeap = ESP.getFreeHeap();
    int clientCount = ws.count();
    float errorRate = queueStats.totalMessages > 0 ? 
                     (float)queueStats.queueErrors / queueStats.totalMessages : 0.0f;
    
    // Adaptive logic
    if (freeHeap < 30000 || errorRate > 0.1f) {
        // Reduce streaming rate
        for (auto& info : clientInfo) {
            info.preferredRate = max(2, info.preferredRate - 1);
        }
        Serial.println("Reduced streaming rates due to system stress");
    } else if (freeHeap > 80000 && errorRate < 0.02f && clientCount <= 2) {
        // Increase streaming rate
        for (auto& info : clientInfo) {
            info.preferredRate = min(15, info.preferredRate + 1);
        }
        Serial.println("Increased streaming rates due to good performance");
    }
    
    lastAdaptation = now;
}
