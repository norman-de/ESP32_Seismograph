#include "time_manager.h"

TimeManager::TimeManager() : timeClient(ntpUDP, NTP_SERVER1, TIMEZONE_OFFSET) {
    detailedLoggingEnabled = false;
    initialized = false;
    lastSync = 0;
    bootTime = 0;
}

bool TimeManager::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        if (detailedLoggingEnabled) Serial.println("ERROR: WiFi not connected, cannot initialize Time Manager");
        return false;
    }
    
    if (detailedLoggingEnabled) Serial.println("Initializing NTP client...");
    
    timeClient.begin();
    timeClient.setTimeOffset(TIMEZONE_OFFSET);
    
    // Try to sync with NTP
    if (syncWithNTP()) {
        initialized = true;
        bootTime = getEpochTime() - (millis() / 1000);
        if (detailedLoggingEnabled) Serial.printf("Time Manager initialized. Current time: %s\n", getFormattedDateTime().c_str());
        return true;
    } else {
        if (detailedLoggingEnabled) Serial.println("WARNING: NTP sync failed, time may be inaccurate");
        initialized = false;
        return false;
    }
}

void TimeManager::loop() {
    if (!initialized) return;
    
    unsigned long currentTime = millis();
    
    // Sync with NTP periodically
    if (currentTime - lastSync >= NTP_SYNC_INTERVAL) {
        if (WiFi.status() == WL_CONNECTED) {
            syncWithNTP();
        }
    }
    
    // Update time client
    timeClient.update();
}

bool TimeManager::syncWithNTP() {
    if (detailedLoggingEnabled) Serial.print("Syncing with NTP server...");
    
    // Try primary server first
    timeClient.setPoolServerName(NTP_SERVER1);
    if (timeClient.forceUpdate()) {
        lastSync = millis();
        if (detailedLoggingEnabled) Serial.printf(" success! Time: %s\n", getFormattedDateTime().c_str());
        return true;
    }
    
    // Try secondary server
    if (detailedLoggingEnabled) Serial.print(" trying secondary server...");
    timeClient.setPoolServerName(NTP_SERVER2);
    if (timeClient.forceUpdate()) {
        lastSync = millis();
        if (detailedLoggingEnabled) Serial.printf(" success! Time: %s\n", getFormattedDateTime().c_str());
        return true;
    }
    
    // Try tertiary server
    if (detailedLoggingEnabled) Serial.print(" trying tertiary server...");
    timeClient.setPoolServerName(NTP_SERVER3);
    if (timeClient.forceUpdate()) {
        lastSync = millis();
        if (detailedLoggingEnabled) Serial.printf(" success! Time: %s\n", getFormattedDateTime().c_str());
        return true;
    }
    
    if (detailedLoggingEnabled) Serial.println(" failed!");
    return false;
}

String TimeManager::getFormattedTime() {
    if (!initialized) {
        unsigned long seconds = millis() / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        
        seconds %= 60;
        minutes %= 60;
        hours %= 24;
        
        char buffer[16];
        sprintf(buffer, "%02lu:%02lu:%02lu", hours, minutes, seconds);
        return String(buffer);
    }
    
    return timeClient.getFormattedTime();
}

String TimeManager::getFormattedDate() {
    if (!initialized) {
        unsigned long days = millis() / 86400000;
        return "Day " + String(days);
    }
    
    unsigned long epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);
    
    char buffer[16];
    sprintf(buffer, "%04d-%02d-%02d", 
            ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
    
    return String(buffer);
}

String TimeManager::getFormattedDateTime() {
    if (!initialized) {
        return getFormattedTime() + " (Boot time)";
    }
    
    return getFormattedDate() + " " + getFormattedTime();
}

unsigned long TimeManager::getEpochTime() {
    if (!initialized) {
        return bootTime + (millis() / 1000);
    }
    
    return timeClient.getEpochTime();
}

unsigned long TimeManager::getUptime() {
    return millis() / 1000;
}

bool TimeManager::isTimeValid() {
    return initialized && (millis() - lastSync < NTP_SYNC_INTERVAL * 2);
}

void TimeManager::forceSync() {
    if (WiFi.status() == WL_CONNECTED) {
        if (detailedLoggingEnabled) Serial.println("Forcing NTP sync...");
        syncWithNTP();
    } else {
        if (detailedLoggingEnabled) Serial.println("Cannot force sync: WiFi not connected");
    }
}

void TimeManager::setTimezone() {
    // This is handled by the NTPClient timeOffset
    // Additional timezone logic could be added here if needed
}
