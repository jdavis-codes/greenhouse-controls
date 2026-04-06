#ifndef GREENHOUSE_TELEGRAM_H
#define GREENHOUSE_TELEGRAM_H

#include <Arduino.h>
#include <FastBot2.h>
#include <RTClib.h>
#include <FFat.h>
#include <SD.h>

// --- Data Structures ---

struct SensorReading {
    DateTime timestamp;
    float value;
};

struct SensorHistory {
    const char* name;
    const char* unit;
    const char* color;
    int count;
    SensorReading* readings;
};

struct EventReading {
    DateTime timestamp;
    bool state;
};

struct EventHistory {
    const char* name;
    const char* emojiOn;
    const char* emojiOff;
    const char* onStr;
    const char* offStr;
    const char* color;
    int count;
    EventReading* readings;
};

struct SettingsParameter {
    String icon;
    String name;
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
    
    void setup();
    void tick();

    // Data references to update
    void setSensorHistories(SensorHistory* histories, int count);
    void setEventHistories(EventHistory* events, int count);
    void setSettings(SettingsParameter* settings, int count);

    // Callbacks for manual controls (to tie into your business logic)
    void setControlCallbacks(void (*onFan)(bool), void (*onIrrigation)(bool), void (*onSides)(bool));

private:
    FastBot2 bot;
    BotOperatingMode operatingMode;
    
    SensorHistory* sensorHistories;
    int numSensors;
    
    EventHistory* eventHistories;
    int numEvents;
    
    SettingsParameter* settings;
    int numSettings;

    // Callbacks
    void (*fanCallback)(bool);
    void (*irrigationCallback)(bool);
    void (*sidesCallback)(bool);

    // Context state
    uint32_t dashboardMsgID;
    fb::ID targetChatID;
    int activeConfigIndex;
    bool awaitValue;

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
    void sendSvgGraph(fb::ID chatID);
    
    // Helpers
    String formatTime(DateTime dt);
};

#endif // GREENHOUSE_TELEGRAM_H
