#include "GreenhouseControlNode.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#else
#include <EEPROM.h>
#endif

#include "RYLR_LoRaAT_Software_Serial.h"

namespace {
constexpr uint32_t PERSIST_MAGIC = 0x47484332UL;

void debugLogf(const char* format, ...) {
    char buffer[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
}
}

GreenhouseControlNode::GreenhouseControlNode()
    : radio(nullptr),
      remoteAddress(0),
            storageReady(false),
            activityBlinkUntil(0),
      lastSendMillis(0),
      greenhouseTemp(82),
      greenhouseHumidity(58),
      soilMoisture(45),
      insolation(73),
      fanOn(false),
      sidesUp(false),
      irrigationOn(false),
      targetTemp1(80.0f),
      targetTemp2(85.0f),
      tempDelta(4.0f),
      targetMoisture(50.0f),
      moistureDelta(10.0f) {}

void GreenhouseControlNode::begin(RYLR_LoRaAT_Software_Serial* radioLink, uint16_t remoteNodeAddress) {
    radio = radioLink;
    remoteAddress = remoteNodeAddress;

    loadPersistedState();
    initializeEventAlertState();
    evaluateSensorAlerts();
    sendFullStateSync();
    sendTelemetry();
}

void GreenhouseControlNode::setupStatusLed() {
#if defined(ARDUINO_ARCH_ESP32) && defined(LEDR) && defined(LEDG) && defined(LEDB)
    pinMode(LEDR, OUTPUT);
    pinMode(LEDG, OUTPUT);
    pinMode(LEDB, OUTPUT);
#elif defined(LED_BUILTIN)
    pinMode(LED_BUILTIN, OUTPUT);
#endif

    showIdleStatusLed();
}

void GreenhouseControlNode::noteActivity(unsigned long now) {
    activityBlinkUntil = now + ACTIVITY_BLINK_MS;
    writeStatusLed(false, true, false);
}

void GreenhouseControlNode::tick(unsigned long now) {
    updateDemoSensors(now);
    evaluateSensorAlerts();
    updateStatusLed(now);

    if (now - lastSendMillis >= SEND_INTERVAL_MS) {
        lastSendMillis = now;
        sendTelemetry();
    }
}

void GreenhouseControlNode::handleIncomingMessage(const char* data) {
    if (!data || !data[0]) return;

    if (strncmp(data, "C,", 2) == 0) {
        char deviceKey[16];
        int value = 0;
        if (sscanf(data + 2, "%15[^,],%d", deviceKey, &value) == 2 && handleControlCommand(deviceKey, value)) {
            sendTelemetry();
        }
        return;
    }

    if (strncmp(data, "S,", 2) == 0) {
        char key[16];
        float value = 0.0f;
        if (sscanf(data + 2, "%15[^,],%f", key, &value) == 2) {
            handleSettingCommand(key, value);
        }
        return;
    }

    if (strncmp(data, "A,S,", 4) == 0) {
        int sensorIndex = -1;
        char direction = '\0';
        int enabled = 0;
        float threshold = 0.0f;
        if (sscanf(data, "A,S,%d,%c,%d,%f", &sensorIndex, &direction, &enabled, &threshold) == 4) {
            handleSensorAlertCommand(sensorIndex, direction, enabled, threshold);
        }
        return;
    }

    if (strncmp(data, "A,E,", 4) == 0) {
        int eventIndex = -1;
        int enabled = 0;
        if (sscanf(data, "A,E,%d,%d", &eventIndex, &enabled) == 2) {
            handleEventAlertCommand(eventIndex, enabled);
        }
        return;
    }

    if (strcmp(data, "Q,SYNC") == 0) {
        sendFullStateSync();
    }
}

void GreenhouseControlNode::loadPersistedState() {
#if defined(ARDUINO_ARCH_ESP32)
    Preferences preferences;
    storageReady = preferences.begin("ghctrl", true);
    if (!storageReady) return;

    PersistedState state = {};
    size_t bytesRead = preferences.getBytes("state", &state, sizeof(state));
    preferences.end();
#else
    storageReady = true;
    PersistedState state = {};
    EEPROM.get(0, state);
    size_t bytesRead = sizeof(state);
#endif

    if (bytesRead == sizeof(state) && state.magic == PERSIST_MAGIC) {
        applyPersistedState(state);
    }
}

void GreenhouseControlNode::persistState() {
    if (!storageReady) return;

    PersistedState state = {};
    state.magic = PERSIST_MAGIC;
    state.targetTemp1 = targetTemp1;
    state.targetTemp2 = targetTemp2;
    state.tempDelta = tempDelta;
    state.targetMoisture = targetMoisture;
    state.moistureDelta = moistureDelta;

    for (int i = 0; i < SENSOR_COUNT; i++) {
        state.sensorLowEnabled[i] = sensorAlerts[i].lowEnabled ? 1 : 0;
        state.sensorLowThreshold[i] = sensorAlerts[i].lowThreshold;
        state.sensorHighEnabled[i] = sensorAlerts[i].highEnabled ? 1 : 0;
        state.sensorHighThreshold[i] = sensorAlerts[i].highThreshold;
    }

    for (int i = 0; i < EVENT_COUNT; i++) {
        state.eventEnabled[i] = eventAlerts[i].enabled ? 1 : 0;
    }

#if defined(ARDUINO_ARCH_ESP32)
    Preferences preferences;
    if (!preferences.begin("ghctrl", false)) return;
    preferences.putBytes("state", &state, sizeof(state));
    preferences.end();
#else
    EEPROM.put(0, state);
#endif
}

void GreenhouseControlNode::applyPersistedState(const PersistedState& state) {
    targetTemp1 = state.targetTemp1;
    targetTemp2 = state.targetTemp2;
    tempDelta = state.tempDelta;
    targetMoisture = state.targetMoisture;
    moistureDelta = state.moistureDelta;

    for (int i = 0; i < SENSOR_COUNT; i++) {
        sensorAlerts[i].lowEnabled = state.sensorLowEnabled[i] != 0;
        sensorAlerts[i].lowThreshold = state.sensorLowThreshold[i];
        sensorAlerts[i].lowActive = false;
        sensorAlerts[i].highEnabled = state.sensorHighEnabled[i] != 0;
        sensorAlerts[i].highThreshold = state.sensorHighThreshold[i];
        sensorAlerts[i].highActive = false;
    }

    for (int i = 0; i < EVENT_COUNT; i++) {
        eventAlerts[i].enabled = state.eventEnabled[i] != 0;
    }
}

void GreenhouseControlNode::initializeEventAlertState() {
    for (int i = 0; i < EVENT_COUNT; i++) {
        eventAlerts[i].lastStateKnown = true;
        eventAlerts[i].lastState = eventState(i);
    }
}

void GreenhouseControlNode::saveSettingValue(const char* key, float value) {
    (void)key;
    (void)value;
    persistState();
}

void GreenhouseControlNode::saveSensorAlert(uint8_t sensorIndex) {
    if (sensorIndex >= SENSOR_COUNT) return;
    persistState();
}

void GreenhouseControlNode::saveEventAlert(uint8_t eventIndex) {
    if (eventIndex >= EVENT_COUNT) return;
    persistState();
}

void GreenhouseControlNode::writeStatusLed(bool redOn, bool greenOn, bool blueOn) {
#if defined(ARDUINO_ARCH_ESP32) && defined(LEDR) && defined(LEDG) && defined(LEDB)
    digitalWrite(LEDR, redOn ? LOW : HIGH);
    digitalWrite(LEDG, greenOn ? LOW : HIGH);
    digitalWrite(LEDB, blueOn ? LOW : HIGH);
#elif defined(LED_BUILTIN)
    digitalWrite(LED_BUILTIN, greenOn ? HIGH : LOW);
#else
    (void)redOn;
    (void)greenOn;
    (void)blueOn;
#endif
}

void GreenhouseControlNode::showIdleStatusLed() {
    writeStatusLed(false, false, true);
}

void GreenhouseControlNode::updateStatusLed(unsigned long now) {
    if (now >= activityBlinkUntil) {
        showIdleStatusLed();
    }
}

void GreenhouseControlNode::updateDemoSensors(unsigned long now) {
    greenhouseTemp = 78 + (now / 10000) % 15;
    greenhouseHumidity = 50 + (now / 8000) % 20;
    soilMoisture = 30 + (now / 12000) % 40;
    insolation = 20 + (now / 6000) % 60;
}

void GreenhouseControlNode::sendPacket(const char* payload) {
    if (!radio || !payload || !payload[0]) return;

    debugLogf("TX -> [%d]: %s\n", remoteAddress, payload);
    noteActivity(millis());
    radio->startTxMessage();
    radio->addTxData(payload);
    int result = radio->sendTxMessage(remoteAddress);
    debugLogf("Send result: %d\n", result);
}

void GreenhouseControlNode::sendTelemetry() {
    char payload[64];
    snprintf(payload, sizeof(payload), "D,%d,%d,%d,%d,%d,%d,%d",
             greenhouseTemp, greenhouseHumidity, soilMoisture, insolation,
             fanOn ? 1 : 0, sidesUp ? 1 : 0, irrigationOn ? 1 : 0);
    sendPacket(payload);
}

void GreenhouseControlNode::sendSettingSync(const char* key, float value) {
    char payload[32];
    snprintf(payload, sizeof(payload), "R,S,%s,%.1f", key, (double)value);
    sendPacket(payload);
}

void GreenhouseControlNode::sendSensorAlertSync(uint8_t sensorIndex) {
    if (sensorIndex >= SENSOR_COUNT) return;

    char payload[64];
    const SensorAlertConfig& alert = sensorAlerts[sensorIndex];
    snprintf(payload, sizeof(payload), "R,A,%u,%d,%.1f,%d,%.1f",
             sensorIndex,
             alert.lowEnabled ? 1 : 0, (double)alert.lowThreshold,
             alert.highEnabled ? 1 : 0, (double)alert.highThreshold);
    sendPacket(payload);
}

void GreenhouseControlNode::sendEventAlertSync(uint8_t eventIndex) {
    if (eventIndex >= EVENT_COUNT) return;

    char payload[32];
    snprintf(payload, sizeof(payload), "R,E,%u,%d", eventIndex, eventAlerts[eventIndex].enabled ? 1 : 0);
    sendPacket(payload);
}

void GreenhouseControlNode::sendFullStateSync() {
    sendSettingSync("TEMP1", targetTemp1);
    sendSettingSync("TEMP2", targetTemp2);
    sendSettingSync("TDELTA", tempDelta);
    sendSettingSync("MOIST", targetMoisture);
    sendSettingSync("MDELTA", moistureDelta);

    for (int i = 0; i < SENSOR_COUNT; i++) {
        sendSensorAlertSync(i);
    }
    for (int i = 0; i < EVENT_COUNT; i++) {
        sendEventAlertSync(i);
    }
}

void GreenhouseControlNode::evaluateSensorAlerts() {
    for (int i = 0; i < SENSOR_COUNT; i++) {
        float value = sensorValue(i);
        SensorAlertConfig& alert = sensorAlerts[i];

        bool lowTriggered = alert.lowEnabled && value <= alert.lowThreshold;
        if (lowTriggered && !alert.lowActive) {
            sendSensorAlertNotification(i, 'L', value, alert.lowThreshold);
        }
        alert.lowActive = lowTriggered;

        bool highTriggered = alert.highEnabled && value >= alert.highThreshold;
        if (highTriggered && !alert.highActive) {
            sendSensorAlertNotification(i, 'H', value, alert.highThreshold);
        }
        alert.highActive = highTriggered;
    }
}

void GreenhouseControlNode::sendSensorAlertNotification(uint8_t sensorIndex, char direction, float currentValue, float threshold) {
    char payload[64];
    snprintf(payload, sizeof(payload), "N,S,%u,%c,%.1f,%.1f", sensorIndex, direction, (double)currentValue, (double)threshold);
    sendPacket(payload);
}

void GreenhouseControlNode::setEventState(uint8_t eventIndex, bool newState, bool shouldNotify) {
    if (eventIndex >= EVENT_COUNT) return;

    bool previousState = eventState(eventIndex);
    switch (eventIndex) {
        case EVENT_FAN:
            fanOn = newState;
            break;
        case EVENT_SIDES:
            sidesUp = newState;
            break;
        case EVENT_IRRIGATION:
            irrigationOn = newState;
            break;
        default:
            return;
    }

    EventAlertConfig& alert = eventAlerts[eventIndex];
    if (!alert.lastStateKnown) {
        alert.lastStateKnown = true;
        alert.lastState = newState;
        return;
    }

    if (previousState != newState && shouldNotify && alert.enabled) {
        sendEventAlertNotification(eventIndex, newState);
    }
    alert.lastState = newState;
}

void GreenhouseControlNode::sendEventAlertNotification(uint8_t eventIndex, bool state) {
    char payload[32];
    snprintf(payload, sizeof(payload), "N,E,%u,%d", eventIndex, state ? 1 : 0);
    sendPacket(payload);
}

float GreenhouseControlNode::sensorValue(uint8_t sensorIndex) const {
    switch (sensorIndex) {
        case SENSOR_GREENHOUSE_TEMP:
            return greenhouseTemp;
        case SENSOR_AMBIENT_TEMP:
            return greenhouseTemp - 5.0f;
        case SENSOR_GREENHOUSE_HUMIDITY:
            return greenhouseHumidity;
        case SENSOR_INSOLATION:
            return insolation;
        case SENSOR_SOIL_MOISTURE:
            return soilMoisture;
        default:
            return 0.0f;
    }
}

bool GreenhouseControlNode::eventState(uint8_t eventIndex) const {
    switch (eventIndex) {
        case EVENT_FAN:
            return fanOn;
        case EVENT_SIDES:
            return sidesUp;
        case EVENT_IRRIGATION:
            return irrigationOn;
        default:
            return false;
    }
}

bool GreenhouseControlNode::handleControlCommand(const char* deviceKey, int value) {
    bool state = value == 1;

    if (strcmp(deviceKey, "FAN") == 0) {
        setEventState(EVENT_FAN, state, true);
        debugLogf("Fan set %s via LoRa\n", fanOn ? "ON" : "OFF");
        return true;
    }
    if (strcmp(deviceKey, "SIDES") == 0) {
        setEventState(EVENT_SIDES, state, true);
        debugLogf("Sides set %s via LoRa\n", sidesUp ? "UP" : "DOWN");
        return true;
    }
    if (strcmp(deviceKey, "WATER") == 0) {
        setEventState(EVENT_IRRIGATION, state, true);
        debugLogf("Irrigation set %s via LoRa\n", irrigationOn ? "ON" : "OFF");
        return true;
    }

    debugLogf("Unknown control command: %s\n", deviceKey);
    return false;
}

bool GreenhouseControlNode::handleSettingCommand(const char* key, float value) {
    float* target = nullptr;

    if (strcmp(key, "TEMP1") == 0) target = &targetTemp1;
    else if (strcmp(key, "TEMP2") == 0) target = &targetTemp2;
    else if (strcmp(key, "TDELTA") == 0) target = &tempDelta;
    else if (strcmp(key, "MOIST") == 0) target = &targetMoisture;
    else if (strcmp(key, "MDELTA") == 0) target = &moistureDelta;

    if (!target) {
        debugLogf("Unknown setpoint key: %s\n", key);
        return false;
    }

    *target = value;
    saveSettingValue(key, value);
    sendSettingSync(key, value);

    debugLogf("SETPOINT %s = %.1f\n", key, value);
    return true;
}

void GreenhouseControlNode::handleSensorAlertCommand(int sensorIndex, char direction, int enabled, float threshold) {
    if (sensorIndex < 0 || sensorIndex >= SENSOR_COUNT) return;

    SensorAlertConfig& alert = sensorAlerts[sensorIndex];
    bool enableFlag = enabled == 1;
    if (direction == 'L') {
        alert.lowEnabled = enableFlag;
        alert.lowThreshold = threshold;
        alert.lowActive = enableFlag && sensorValue(sensorIndex) <= threshold;
    } else if (direction == 'H') {
        alert.highEnabled = enableFlag;
        alert.highThreshold = threshold;
        alert.highActive = enableFlag && sensorValue(sensorIndex) >= threshold;
    } else {
        return;
    }

    saveSensorAlert(sensorIndex);
    sendSensorAlertSync(sensorIndex);
}

void GreenhouseControlNode::handleEventAlertCommand(int eventIndex, int enabled) {
    if (eventIndex < 0 || eventIndex >= EVENT_COUNT) return;

    eventAlerts[eventIndex].enabled = enabled == 1;
    eventAlerts[eventIndex].lastStateKnown = true;
    eventAlerts[eventIndex].lastState = eventState(eventIndex);
    saveEventAlert(eventIndex);
    sendEventAlertSync(eventIndex);
}