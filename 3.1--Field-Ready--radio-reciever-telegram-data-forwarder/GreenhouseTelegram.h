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
    const char* key;
    const char* emojiOn;
    const char* emojiOff;
    const char* onStr;
    const char* offStr;
    const char* color;
    bool LogEntry::*stateField;
};

struct SettingsParameter {
    String icon;
    String name;
    const char* key;     // Short identifier for LoRa commands (e.g. "TEMP1")
    float* valueRef;
    float minValue;
    float maxValue;
    String unit;
};

enum AlertThresholdType {
    ALERT_THRESHOLD_NONE,
    ALERT_THRESHOLD_LOW,
    ALERT_THRESHOLD_HIGH
};

struct SensorAlertConfig {
    bool lowEnabled;
    float lowThreshold;
    bool lowActive;
    bool highEnabled;
    float highThreshold;
    bool highActive;

    SensorAlertConfig()
        : lowEnabled(false), lowThreshold(0.0f), lowActive(false),
          highEnabled(false), highThreshold(0.0f), highActive(false) {}
};

struct EventAlertConfig {
    bool enabled;
    bool lastStateKnown;
    bool lastState;

    EventAlertConfig() : enabled(true), lastStateKnown(false), lastState(false) {}
};

enum BotOperatingMode {
    MODE_CHAT,
    MODE_DASHBOARD
};

struct ChatSessionState {
    String chatID;
    BotOperatingMode mode;
    uint32_t dashboardMsgID;
    bool dashboardLiveView;

    ChatSessionState() : chatID(""), mode(MODE_CHAT), dashboardMsgID(0), dashboardLiveView(false) {}
};

// --- Main Bot Class ---

class GreenhouseTelegramBot {
public:
    GreenhouseTelegramBot(const String& token, BotOperatingMode mode);
    
    // Convenience all-in-one setup that inits PSRAM and creates log buffer
    void begin(SensorMetadata* sensors, int numS,
               EventMetadata* events, int numE,
               SettingsParameter* params, int numP);

    // Elegant C++ template wrapper to deduce array sizes automatically
    template <size_t numS, size_t numE, size_t numP>
    void begin(SensorMetadata (&sensors)[numS],
               EventMetadata (&events)[numE],
               SettingsParameter (&params)[numP]) {
        begin(&sensors[0], (int)numS, &events[0], (int)numE, &params[0], (int)numP);
    }

    void setup();
    void tick();
    void refreshDashboard();
    void setLogFilePath(const String& path);
    void updateLinkMetrics(int rssi, int snr);
    void sendTextToChat(const char* chatID, const String& text, bool html = false);
    void broadcastAlertMessage(const String& text, bool html = true);
    void configureKnownChats(const char* personalChatID, const char* groupChatID);
    void sendBootAnnouncements();
    bool syncSettingValue(const char* key, float value);
    void syncSensorAlertConfig(int sensorIdx, bool lowEnabled, float lowThreshold, bool highEnabled, float highThreshold);
    void syncEventAlertConfig(int eventIdx, bool enabled);

    // Call this after a new log entry is pushed to the buffer
    void evaluateAlerts();

    // Data references to update
    void setLogBuffer(RingBuffer* buffer);
    RingBuffer* getLogBuffer() { return logBuffer; }
    void setSensorMetadata(SensorMetadata* metadata, int count);
    void setEventMetadata(EventMetadata* metadata, int count);
    void setSettings(SettingsParameter* settings, int count);

    // Settings change callback (called whenever a setpoint is adjusted)
    void onSettingChanged(void (*cb)(const char* key, float value)) { settingChangedCb = cb; }
    void onSensorAlertChanged(void (*cb)(int sensorIdx, AlertThresholdType thresholdType, bool enabled, float threshold)) { sensorAlertChangedCb = cb; }
    void onEventAlertChanged(void (*cb)(int eventIdx, bool enabled)) { eventAlertChangedCb = cb; }
    void onControlCommand(void (*cb)(const char* key, bool state)) { controlCommandCb = cb; }

private:
    void (*settingChangedCb)(const char* key, float value) = nullptr;
    void (*sensorAlertChangedCb)(int sensorIdx, AlertThresholdType thresholdType, bool enabled, float threshold) = nullptr;
    void (*eventAlertChangedCb)(int eventIdx, bool enabled) = nullptr;
    void (*controlCommandCb)(const char* key, bool state) = nullptr;
    FastBot2 bot;
    BotOperatingMode operatingMode;
    
    RingBuffer* logBuffer;
    
    SensorMetadata* sensorMetadata;
    int numSensors;
    
    EventMetadata* eventMetadata;
    int numEvents;
    
    SettingsParameter* settings;
    int numSettings;
    SensorAlertConfig* sensorAlerts;
    EventAlertConfig* eventAlerts;

    // Context state
    String logFilePath;
    int activeConfigIndex;
    String pendingConfigChatID;
    int pendingAlertSensorIndex;
    AlertThresholdType pendingAlertThresholdType;
    String pendingAlertChatID;
    String cachedDashboardBody;
    ChatSessionState personalChatSession;
    ChatSessionState groupChatSession;
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
    void sendControlsMenu(fb::ID chatID, uint32_t editMsgID = 0);
    void sendConfigMainMenu(fb::ID chatID, uint32_t editMsgID = 0);
    void sendConfigEditMenu(fb::ID chatID, uint32_t msgID, int paramIndex);
    void sendAlertsMenu(fb::ID chatID, uint32_t editMsgID = 0);
    void sendSensorAlertsMenu(fb::ID chatID, uint32_t editMsgID = 0);
    void sendSensorAlertDetailMenu(fb::ID chatID, uint32_t editMsgID, int sensorIndex);
    void sendEventAlertsMenu(fb::ID chatID, uint32_t editMsgID = 0);
    void sendGuideMenu(fb::ID chatID, uint32_t editMsgID);
    void sendSvgMenu(fb::ID chatID, uint32_t editMsgID);
    
    // Storage
    void loadPersistedAlerts();
    void saveAlertSettings();

    // Graph generation
    void sendSnapshot(fb::ID chatID);
    uint32_t sendUnicodeGraph(fb::ID chatID, uint32_t editMsgID = 0);
    bool sendLogFile(fb::ID chatID);
    void sendSvgGraph(fb::ID chatID);
    
    // Helpers
    void installBotCommands();
    String formatTime(DateTime dt);
    String formatDuration(unsigned long elapsedMs) const;
    bool isLinkConnected() const;
    const char* getLinkQualityLabel() const;
    const char* getLinkQualityBar() const;
    String buildDashboardStatusHeader() const;
    String buildGuideText() const;
    String buildConfigText() const;
    String buildControlsText() const;
    String buildConfigPrompt(int settingIdx) const;
    String buildAlertPrompt(int sensorIdx, AlertThresholdType thresholdType) const;
    String buildModeStatusText(BotOperatingMode mode) const;
    String buildBootText(BotOperatingMode mode) const;
    String buildSensorAlertSummary(int sensorIdx) const;
    String buildEventAlertSummary(int eventIdx) const;
    String formatSensorReading(int sensorIdx, float value) const;
    String extractCommandName(const String& text, String& args) const;
    String normalizeToken(const String& text) const;
    String chatIDKey(fb::ID chatID) const;
    int findSettingIndex(const char* key) const;
    int findSettingIndex(const String& token) const;
    int findEventIndex(const String& token) const;
    int findSensorIndex(const String& token) const;
    void clearPendingAlertEdit();
    bool hasPendingAlertEdit(const String& chatID) const;
    void notifyAlertChats(const String& text);
    ChatSessionState* findSessionByChatID(const String& chatID);
    const ChatSessionState* findSessionByChatID(const String& chatID) const;
    BotOperatingMode getModeForChat(const String& chatID) const;
    void setModeForChat(const String& chatID, BotOperatingMode mode);
};

#endif // GREENHOUSE_TELEGRAM_H
