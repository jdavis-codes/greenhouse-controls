#ifndef GREENHOUSE_CONTROL_NODE_H
#define GREENHOUSE_CONTROL_NODE_H

#include <Arduino.h>

class RYLR_LoRaAT;

enum telegram_data_pipe {
    radio,
    uart_rx_tx
};

class GreenhouseControlNode {
public:
    struct SensorBinding {
        const char* key;
        int* valueRef;
    };

    struct EventBinding {
        const char* key;
        bool* stateRef;
        void (*onSetState)(bool);
    };

    struct SettingBinding {
        const char* key;
        float* valueRef;
    };

    typedef void (*SampleCallback)(unsigned long now);

    GreenhouseControlNode();

    void configure(SensorBinding* sensorList,
                   uint8_t sensorListCount,
                   EventBinding* eventList,
                   uint8_t eventListCount,
                   SettingBinding* settingList,
                   uint8_t settingListCount,
                   SampleCallback sampleFn = nullptr);

    template <size_t numS, size_t numE, size_t numP>
    void configure(SensorBinding (&sensorList)[numS],
                   EventBinding (&eventList)[numE],
                   SettingBinding (&settingList)[numP],
                   SampleCallback sampleFn = nullptr) {
        configure(sensorList, numS, eventList, numE, settingList, numP, sampleFn);
    }

    void begin(RYLR_LoRaAT* radioLink,
               uint16_t remoteNodeAddress,
               telegram_data_pipe dataPipe,
               Stream* serialPipe = nullptr);

    bool readLineFromSerial(char* buffer, size_t bufferLen);
    void setupStatusLed();
    void noteActivity(unsigned long now);
    void tick(unsigned long now);
    void handleIncomingMessage(const char* data);

private:
    static const unsigned long ACTIVITY_BLINK_MS = 120;
    static const unsigned long SEND_INTERVAL_MS = 15000;
    static const uint8_t MAX_SENSOR_BINDINGS = 8;
    static const uint8_t MAX_EVENT_BINDINGS = 8;
    static const uint8_t MAX_SETTING_BINDINGS = 8;

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

    struct PersistedState {
        uint32_t magic;
        uint8_t sensorCount;
        uint8_t eventCount;
        uint8_t settingCount;
        float settingValues[MAX_SETTING_BINDINGS];
        uint8_t sensorLowEnabled[MAX_SENSOR_BINDINGS];
        float sensorLowThreshold[MAX_SENSOR_BINDINGS];
        uint8_t sensorHighEnabled[MAX_SENSOR_BINDINGS];
        float sensorHighThreshold[MAX_SENSOR_BINDINGS];
        uint8_t eventEnabled[MAX_EVENT_BINDINGS];
    };

    RYLR_LoRaAT* radio;
    telegram_data_pipe activeDataPipe;
    Stream* serialDataPipe;
    size_t serialReadIndex;
    uint16_t remoteAddress;
    bool storageReady;
    unsigned long activityBlinkUntil;
    unsigned long lastSendMillis;

    SensorBinding* sensors;
    uint8_t sensorCount;
    EventBinding* events;
    uint8_t eventCount;
    SettingBinding* settings;
    uint8_t settingCount;
    SampleCallback sampleCallback;

    SensorAlertConfig sensorAlerts[MAX_SENSOR_BINDINGS];
    EventAlertConfig eventAlerts[MAX_EVENT_BINDINGS];

    void loadPersistedState();
    void persistState();
    void applyPersistedState(const PersistedState& state);
    void initializeEventAlertState();
    void saveSettingValue(const char* key, float value);
    void saveSensorAlert(uint8_t sensorIndex);
    void saveEventAlert(uint8_t eventIndex);
    void writeStatusLed(bool redOn, bool greenOn, bool blueOn);
    void showIdleStatusLed();
    void updateStatusLed(unsigned long now);

    void sendPacket(const char* payload);
    void sendTelemetry();
    void sendSettingSync(const char* key, float value);
    void sendSensorAlertSync(uint8_t sensorIndex);
    void sendEventAlertSync(uint8_t eventIndex);
    void sendFullStateSync();

    void evaluateSensorAlerts();
    void sendSensorAlertNotification(uint8_t sensorIndex, char direction, float currentValue, float threshold);
    void setEventState(uint8_t eventIndex, bool newState, bool shouldNotify);
    void sendEventAlertNotification(uint8_t eventIndex, bool state);

    float sensorValue(uint8_t sensorIndex) const;
    bool eventState(uint8_t eventIndex) const;

    int findEventIndexByKey(const char* key) const;
    int findSettingIndexByKey(const char* key) const;

    bool handleControlCommand(const char* deviceKey, int value);
    bool handleSettingCommand(const char* key, float value);
    void handleSensorAlertCommand(int sensorIndex, char direction, int enabled, float threshold);
    void handleEventAlertCommand(int eventIndex, int enabled);
};

#endif