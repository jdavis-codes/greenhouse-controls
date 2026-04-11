#ifndef GREENHOUSE_CONTROL_NODE_H
#define GREENHOUSE_CONTROL_NODE_H

#include <Arduino.h>

class RYLR_LoRaAT_Software_Serial;

class GreenhouseControlNode {
public:
    GreenhouseControlNode();

    void begin(RYLR_LoRaAT_Software_Serial* radioLink, uint16_t remoteNodeAddress);
    void setupStatusLed();
    void noteActivity(unsigned long now);
    void tick(unsigned long now);
    void handleIncomingMessage(const char* data);

private:
    static const unsigned long ACTIVITY_BLINK_MS = 120;
    static const unsigned long SEND_INTERVAL_MS = 15000;

    enum SensorIndex {
        SENSOR_GREENHOUSE_TEMP = 0,
        SENSOR_AMBIENT_TEMP,
        SENSOR_GREENHOUSE_HUMIDITY,
        SENSOR_INSOLATION,
        SENSOR_SOIL_MOISTURE,
        SENSOR_COUNT
    };

    enum EventIndex {
        EVENT_FAN = 0,
        EVENT_SIDES,
        EVENT_IRRIGATION,
        EVENT_COUNT
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

    struct PersistedState {
        uint32_t magic;
        float targetTemp1;
        float targetTemp2;
        float tempDelta;
        float targetMoisture;
        float moistureDelta;
        uint8_t sensorLowEnabled[SENSOR_COUNT];
        float sensorLowThreshold[SENSOR_COUNT];
        uint8_t sensorHighEnabled[SENSOR_COUNT];
        float sensorHighThreshold[SENSOR_COUNT];
        uint8_t eventEnabled[EVENT_COUNT];
    };

    RYLR_LoRaAT_Software_Serial* radio;
    uint16_t remoteAddress;
    bool storageReady;
    unsigned long activityBlinkUntil;
    unsigned long lastSendMillis;

    int greenhouseTemp;
    int greenhouseHumidity;
    int soilMoisture;
    int insolation;
    bool fanOn;
    bool sidesUp;
    bool irrigationOn;

    float targetTemp1;
    float targetTemp2;
    float tempDelta;
    float targetMoisture;
    float moistureDelta;

    SensorAlertConfig sensorAlerts[SENSOR_COUNT];
    EventAlertConfig eventAlerts[EVENT_COUNT];

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

    void updateDemoSensors(unsigned long now);
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

    bool handleControlCommand(const char* deviceKey, int value);
    bool handleSettingCommand(const char* key, float value);
    void handleSensorAlertCommand(int sensorIndex, char direction, int enabled, float threshold);
    void handleEventAlertCommand(int eventIndex, int enabled);
};

#endif