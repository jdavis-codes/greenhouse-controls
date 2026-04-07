#ifndef GREENHOUSE_TELEGRAM_H
#define GREENHOUSE_TELEGRAM_H

#include <Arduino.h>
#include <WiFi.h>
#include <FastBot2.h>
#include <time.h>
#include <RTClib.h>
#include <FFat.h>
#include <SD.h>
#include "LogBuffer.h"

// --- Data Structures ---

struct SensorMetadata {
    const char* name;
    const char* unit;
    const char* color;
    float LogEntry::*valueField;
};

struct EventMetadata {
    const char* name;
    const char* emojiOn;
    const char* emojiOff;
    const char* onStr;
    const char* offStr;
    const char* color;
    bool LogEntry::*stateField;
    void (*controlCallback)(bool);
};

struct SettingsParameter {
    String icon;
    String name;
    const char* key;     // Short identifier for LoRa commands (e.g. "TEMP1")
    float* valueRef;
    String unit;
};

enum BotOperatingMode {
    MODE_CHAT,
    MODE_DASHBOARD
};

// --- Main Bot Class ---

class GreenhouseTelegramBot {
public:
    GreenhouseTelegramBot(const String& token, BotOperatingMode mode);
    
    // Convenience all-in-one setup that inits PSRAM and creates log buffer
    void begin(const char* ssid, const char* pass, 
               SensorMetadata* sensors, int numS,
               EventMetadata* events, int numE,
               SettingsParameter* params, int numP);

    // Elegant C++ template wrapper to deduce array sizes automatically
    template <size_t numS, size_t numE, size_t numP>
    void begin(const char* ssid, const char* pass, 
               SensorMetadata (&sensors)[numS],
               EventMetadata (&events)[numE],
               SettingsParameter (&params)[numP]) {
        begin(ssid, pass, sensors, numS, events, numE, params, numP);
    }

    void setup();
    void tick();
    void refreshDashboard();
    void setLogFilePath(const String& path);
    void updateLinkMetrics(int rssi, int snr);

    // Data references to update
    void setLogBuffer(RingBuffer* buffer);
    RingBuffer* getLogBuffer() { return logBuffer; }
    void setSensorMetadata(SensorMetadata* metadata, int count);
    void setEventMetadata(EventMetadata* metadata, int count);
    void setSettings(SettingsParameter* settings, int count);

    // Settings change callback (called whenever a setpoint is adjusted)
    void onSettingChanged(void (*cb)(const char* key, float value)) { settingChangedCb = cb; }

private:
    void (*settingChangedCb)(const char* key, float value) = nullptr;
    FastBot2 bot;
    BotOperatingMode operatingMode;
    
    RingBuffer* logBuffer;
    
    SensorMetadata* sensorMetadata;
    int numSensors;
    
    EventMetadata* eventMetadata;
    int numEvents;
    
    SettingsParameter* settings;
    int numSettings;

    // Context state
    uint32_t dashboardMsgID;
    fb::ID targetChatID;
    bool dashboardLiveView;
    String logFilePath;
    int activeConfigIndex;
    bool awaitValue;
    String cachedDashboardBody;
    unsigned long deviceBootMillis;
    unsigned long lastLoRaRxMillis;
    unsigned long linkConnectedSinceMillis;
    int lastRssi;
    int lastSnr;

    // Handlers
    void handleUpdate(fb::Update& u);
    void handleQuery(fb::Update& u);
    void handleMessage(fb::Update& u);

    // Rendering
    void sendDashboardMainMenu(fb::ID chatID, uint32_t editMsgID = 0);
    void sendControlsMenu(fb::ID chatID, uint32_t editMsgID);
    void sendConfigMainMenu(fb::ID chatID, uint32_t editMsgID = 0);
    void sendConfigEditMenu(fb::ID chatID, uint32_t msgID, int paramIndex);
    void sendSvgMenu(fb::ID chatID, uint32_t editMsgID);
    
    // Graph generation
    uint32_t sendUnicodeGraph(fb::ID chatID, uint32_t editMsgID = 0);
    bool sendLogFile(fb::ID chatID);
    void sendSvgGraph(fb::ID chatID);
    
    // Helpers
    String formatTime(DateTime dt);
    String formatDuration(unsigned long elapsedMs) const;
    bool isLinkConnected() const;
    const char* getLinkQualityLabel() const;
    const char* getLinkQualityBar() const;
    String buildDashboardStatusHeader() const;
};

#endif // GREENHOUSE_TELEGRAM_H
