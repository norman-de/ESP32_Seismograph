#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

// Forward declarations
class Seismograph;
class DataLogger;
class MQTTHandler;
class TimeManager;

class WebServerManager {
private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    bool initialized;
    
    // Module references
    Seismograph* seismographRef;
    DataLogger* dataLoggerRef;
    MQTTHandler* mqttHandlerRef;
    TimeManager* timeManagerRef;
    
    // WebSocket data streaming
    unsigned long lastSensorBroadcast;
    unsigned long lastStatusBroadcast;
    bool realtimeStreamingEnabled;
    
    // Advanced buffering system
    struct SensorDataBuffer {
        static const int BUFFER_SIZE = 10;
        float accelX[BUFFER_SIZE];
        float accelY[BUFFER_SIZE];
        float accelZ[BUFFER_SIZE];
        float magnitude[BUFFER_SIZE];
        unsigned long timestamps[BUFFER_SIZE];
        int writeIndex;
        int sampleCount;
        unsigned long lastUpdate;
        
        SensorDataBuffer() : writeIndex(0), sampleCount(0), lastUpdate(0) {}
        
        void addSample(float x, float y, float z, float mag, unsigned long ts) {
            accelX[writeIndex] = x;
            accelY[writeIndex] = y;
            accelZ[writeIndex] = z;
            magnitude[writeIndex] = mag;
            timestamps[writeIndex] = ts;
            
            writeIndex = (writeIndex + 1) % BUFFER_SIZE;
            if (sampleCount < BUFFER_SIZE) sampleCount++;
            lastUpdate = millis();
        }
        
        bool getAveragedData(float& avgX, float& avgY, float& avgZ, float& avgMag, float& maxMag) {
            if (sampleCount == 0) return false;
            
            avgX = avgY = avgZ = avgMag = maxMag = 0.0f;
            
            for (int i = 0; i < sampleCount; i++) {
                avgX += accelX[i];
                avgY += accelY[i];
                avgZ += accelZ[i];
                avgMag += magnitude[i];
                if (magnitude[i] > maxMag) maxMag = magnitude[i];
            }
            
            avgX /= sampleCount;
            avgY /= sampleCount;
            avgZ /= sampleCount;
            avgMag /= sampleCount;
            
            return true;
        }
    } sensorBuffer;
    
    // Client-specific streaming control
    struct ClientStreamingInfo {
        uint32_t clientId;
        unsigned long lastSent;
        int preferredRate; // Hz
        bool highPriority;
        int queueErrors;
        
        ClientStreamingInfo() : clientId(0), lastSent(0), preferredRate(10), highPriority(false), queueErrors(0) {}
    };
    
    std::vector<ClientStreamingInfo> clientInfo;
    
    // Queue monitoring
    struct QueueStats {
        int totalMessages;
        int queueErrors;
        int successfulSends;
        unsigned long lastReset;
        
        QueueStats() : totalMessages(0), queueErrors(0), successfulSends(0), lastReset(0) {}
    } queueStats;
    
    // Private methods
    void setupRoutes();
    void handleAPI(AsyncWebServerRequest *request);
    void handleData(AsyncWebServerRequest *request);
    void handleEvents(AsyncWebServerRequest *request);
    void handleSeismicEvents(AsyncWebServerRequest *request);
    void handleSystemEvents(AsyncWebServerRequest *request);
    void handleStatus(AsyncWebServerRequest *request);
    void handleCalibrate(AsyncWebServerRequest *request);
    void handleRestart(AsyncWebServerRequest *request);
    void handleSimulate(AsyncWebServerRequest *request);
    void handleScientificStats(AsyncWebServerRequest *request);
    void handleNotFound(AsyncWebServerRequest *request);
    
    // WebSocket methods
    void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    void handleWebSocketMessage(AsyncWebSocketClient *client, uint8_t *data, size_t len);
    void broadcastSensorData();
    void broadcastStatus();
    void broadcastEvent(const String& eventType, const String& data);
    
    // Advanced WebSocket methods
    void managedBroadcast();
    bool canSendToClient(uint32_t clientId);
    void updateClientInfo(uint32_t clientId);
    void cleanupDisconnectedClients();
    void resetQueueStats();
    void printQueueStats();
    bool safeSendToClient(AsyncWebSocketClient* client, const String& message);
    void adaptiveRateControl();
    
    String getContentType(String filename);
    String processor(const String& var);

public:
    WebServerManager();
    bool begin();
    void loop();
    
    // Set module references
    void setReferences(Seismograph* seismo, DataLogger* logger, MQTTHandler* mqtt, TimeManager* time);
    
    // Utility methods
    bool isRunning() { return initialized; }
    bool isInitialized() { return initialized; }
    void sendEventToClients(const String& event);
    void addHttpEndpoint(const char* uri, WebRequestMethodComposite method, std::function<void(AsyncWebServerRequest *request)> onRequest);
    void send(AsyncWebServerRequest *request, int code, const char* contentType, const String& content);
    
    // WebSocket public methods
    void updateSensorData(float accelX, float accelY, float accelZ, float magnitude);
    void sendSeismicEvent(const String& eventType, float magnitude, int level);
    void setRealtimeStreaming(bool enabled) { realtimeStreamingEnabled = enabled; }
    bool isRealtimeStreamingEnabled() { return realtimeStreamingEnabled; }
    int getConnectedClients() { return ws.count(); }
};

#endif // WEB_SERVER_H
