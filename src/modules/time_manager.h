#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "config.h"

class TimeManager {
public:
    bool detailedLoggingEnabled;
private:
    WiFiUDP ntpUDP;
    NTPClient timeClient;
    bool initialized;
    unsigned long lastSync;
    unsigned long bootTime;
    
    // Private methods
    bool syncWithNTP();
    void setTimezone();

public:
    TimeManager();
    bool begin();
    void loop();
    
    // Time retrieval methods
    String getFormattedTime();
    String getFormattedDate();
    String getFormattedDateTime();
    unsigned long getEpochTime();
    unsigned long getUptime();
    
    // Utility methods
    bool isTimeValid();
    void forceSync();
    String formatTimestamp(unsigned long timestamp);
};

#endif // TIME_MANAGER_H
