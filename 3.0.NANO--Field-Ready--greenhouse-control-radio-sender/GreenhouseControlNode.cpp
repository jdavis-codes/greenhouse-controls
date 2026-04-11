#include "GreenhouseControlNode.h"

#include <string.h>
#include <stdlib.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#else
#include <EEPROM.h>
#endif

#include "RYLR_LoRaAT_Software_Serial.h"

namespace {
constexpr uint32_t PERSIST_MAGIC = 0x47484431UL;

// Manual string helpers — avoids linking printf/scanf on AVR (~3-4 KB savings).
int appendStr(char* buf, int pos, int maxLen, const char* s) {
    while (*s && pos + 1 < maxLen) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}
int appendChar(char* buf, int pos, int maxLen, char c) {
    if (pos + 1 < maxLen) { buf[pos++] = c; buf[pos] = '\0'; }
    return pos;
}
int appendInt(char* buf, int pos, int maxLen, long val) {
    char tmp[12];
    ltoa(val, tmp, 10);
    return appendStr(buf, pos, maxLen, tmp);
}
int appendFloat1(char* buf, int pos, int maxLen, float val) {
    if (val < 0) { pos = appendChar(buf, pos, maxLen, '-'); val = -val; }
    int whole = (int)val;
    int frac = (int)(val * 10.0f) % 10;
    pos = appendInt(buf, pos, maxLen, whole);
    pos = appendChar(buf, pos, maxLen, '.');
    return appendInt(buf, pos, maxLen, frac);
}
// Minimal float parser — avoids linking strtod/sscanf float support.
float parseSimpleFloat(const char* s) {
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    long whole = 0;
    while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s - '0'); s++; }
    float result = (float)whole;
    if (*s == '.') {
        s++;
        float frac = 0.0f, div = 10.0f;
        while (*s >= '0' && *s <= '9') { frac += (*s - '0') / div; div *= 10.0f; s++; }
        result += frac;
    }
    return neg ? -result : result;
}
}

GreenhouseControlNode::GreenhouseControlNode()
    : radio(nullptr),
      activeDataPipe(::radio),
      serialDataPipe(nullptr),
      serialReadIndex(0),
      remoteAddress(0),
      storageReady(false),
      activityBlinkUntil(0),
      lastSendMillis(0),
      sensors(nullptr),
      sensorCount(0),
      events(nullptr),
      eventCount(0),
      settings(nullptr),
      settingCount(0),
      sampleCallback(nullptr) {}

void GreenhouseControlNode::configure(SensorBinding* sensorList,
                                      uint8_t sensorListCount,
                                      EventBinding* eventList,
                                      uint8_t eventListCount,
                                      SettingBinding* settingList,
                                      uint8_t settingListCount,
                                      SampleCallback sampleFn) {
    sensors = sensorList;
    sensorCount = min(sensorListCount, MAX_SENSOR_BINDINGS);
    events = eventList;
    eventCount = min(eventListCount, MAX_EVENT_BINDINGS);
    settings = settingList;
    settingCount = min(settingListCount, MAX_SETTING_BINDINGS);
    sampleCallback = sampleFn;
}

void GreenhouseControlNode::begin(RYLR_LoRaAT_Software_Serial* radioLink,
                                  uint16_t remoteNodeAddress,
                                  telegram_data_pipe dataPipe,
                                  Stream* serialPipe) {
    radio = radioLink;
    remoteAddress = remoteNodeAddress;
    activeDataPipe = dataPipe;
    serialDataPipe = serialPipe;
    serialReadIndex = 0;

    loadPersistedState();
#ifndef DISABLE_NODE_ALERTS
    initializeEventAlertState();
    evaluateSensorAlerts();
#endif
    sendFullStateSync();
    sendTelemetry();
}

bool GreenhouseControlNode::readLineFromSerial(char* buffer, size_t bufferLen) {
    if (!serialDataPipe || !buffer || bufferLen < 2) return false;

    while (serialDataPipe->available()) {
        int ch = serialDataPipe->read();
        if (ch < 0) break;

        if (ch == '\r') continue;
        if (ch == '\n') {
            if (serialReadIndex == 0) continue;
            buffer[serialReadIndex] = '\0';
            serialReadIndex = 0;
            return true;
        }

        if (serialReadIndex + 1 < bufferLen) {
            buffer[serialReadIndex++] = (char)ch;
        }
    }

    return false;
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
    if (sampleCallback) {
        sampleCallback(now);
    }

#ifndef DISABLE_NODE_ALERTS
    evaluateSensorAlerts();
#endif
    updateStatusLed(now);

    if (now - lastSendMillis >= SEND_INTERVAL_MS) {
        lastSendMillis = now;
        sendTelemetry();
    }
}

void GreenhouseControlNode::handleIncomingMessage(const char* data) {
    if (!data || !data[0]) return;

    if (strncmp(data, "C,", 2) == 0) {
        const char* p = data + 2;
        const char* comma = strchr(p, ',');
        if (!comma || comma == p || (comma - p) >= 16) return;
        char deviceKey[16];
        memcpy(deviceKey, p, comma - p);
        deviceKey[comma - p] = '\0';
        int value = atoi(comma + 1);
        if (handleControlCommand(deviceKey, value)) {
            sendTelemetry();
        }
        return;
    }

    if (strncmp(data, "S,", 2) == 0) {
        const char* p = data + 2;
        const char* comma = strchr(p, ',');
        if (!comma || comma == p || (comma - p) >= 16) return;
        char key[16];
        memcpy(key, p, comma - p);
        key[comma - p] = '\0';
        float value = parseSimpleFloat(comma + 1);
        handleSettingCommand(key, value);
        return;
    }

#ifndef DISABLE_NODE_ALERTS
    if (strncmp(data, "A,S,", 4) == 0) {
        const char* p = data + 4;
        int sensorIndex = atoi(p);
        p = strchr(p, ','); if (!p) return; p++;
        char direction = *p;
        p = strchr(p, ','); if (!p) return; p++;
        int enabled = atoi(p);
        p = strchr(p, ','); if (!p) return; p++;
        float threshold = parseSimpleFloat(p);
        handleSensorAlertCommand(sensorIndex, direction, enabled, threshold);
        return;
    }

    if (strncmp(data, "A,E,", 4) == 0) {
        const char* p = data + 4;
        int eventIndex = atoi(p);
        p = strchr(p, ','); if (!p) return;
        int enabled = atoi(p + 1);
        handleEventAlertCommand(eventIndex, enabled);
        return;
    }
#endif

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
    state.sensorCount = sensorCount;
    state.eventCount = eventCount;
    state.settingCount = settingCount;

    for (uint8_t i = 0; i < settingCount; i++) {
        state.settingValues[i] = settings[i].valueRef ? *(settings[i].valueRef) : 0.0f;
    }

#ifndef DISABLE_NODE_ALERTS
    for (uint8_t i = 0; i < sensorCount; i++) {
        state.sensorLowEnabled[i] = sensorAlerts[i].lowEnabled ? 1 : 0;
        state.sensorLowThreshold[i] = sensorAlerts[i].lowThreshold;
        state.sensorHighEnabled[i] = sensorAlerts[i].highEnabled ? 1 : 0;
        state.sensorHighThreshold[i] = sensorAlerts[i].highThreshold;
    }

    for (uint8_t i = 0; i < eventCount; i++) {
        state.eventEnabled[i] = eventAlerts[i].enabled ? 1 : 0;
    }
#endif

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
    uint8_t applySettingCount = min(settingCount, state.settingCount);
    for (uint8_t i = 0; i < applySettingCount; i++) {
        if (settings[i].valueRef) {
            *(settings[i].valueRef) = state.settingValues[i];
        }
    }

#ifndef DISABLE_NODE_ALERTS
    uint8_t applySensorCount = min(sensorCount, state.sensorCount);
    for (uint8_t i = 0; i < applySensorCount; i++) {
        sensorAlerts[i].lowEnabled = state.sensorLowEnabled[i] != 0;
        sensorAlerts[i].lowThreshold = state.sensorLowThreshold[i];
        sensorAlerts[i].lowActive = false;
        sensorAlerts[i].highEnabled = state.sensorHighEnabled[i] != 0;
        sensorAlerts[i].highThreshold = state.sensorHighThreshold[i];
        sensorAlerts[i].highActive = false;
    }

    uint8_t applyEventCount = min(eventCount, state.eventCount);
    for (uint8_t i = 0; i < applyEventCount; i++) {
        eventAlerts[i].enabled = state.eventEnabled[i] != 0;
    }
#endif
}

void GreenhouseControlNode::saveSettingValue(const char* key, float value) {
    (void)key;
    (void)value;
    persistState();
}

#ifndef DISABLE_NODE_ALERTS
void GreenhouseControlNode::initializeEventAlertState() {
    for (uint8_t i = 0; i < eventCount; i++) {
        eventAlerts[i].lastStateKnown = true;
        eventAlerts[i].lastState = eventState(i);
    }
}

void GreenhouseControlNode::saveSensorAlert(uint8_t sensorIndex) {
    if (sensorIndex >= sensorCount) return;
    persistState();
}

void GreenhouseControlNode::saveEventAlert(uint8_t eventIndex) {
    if (eventIndex >= eventCount) return;
    persistState();
}
#endif

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

void GreenhouseControlNode::sendPacket(const char* payload) {
    if (!payload || !payload[0]) return;

    if (activeDataPipe == uart_rx_tx && serialDataPipe) {
        noteActivity(millis());
        serialDataPipe->println(payload);
        return;
    }

    if (!radio) return;

    noteActivity(millis());
    radio->startTxMessage();
    radio->addTxData(payload);
    radio->sendTxMessage(remoteAddress);
}

void GreenhouseControlNode::sendTelemetry() {
    if (!sensors || !events || sensorCount == 0 || eventCount == 0) {
        return;
    }

    // Build the packet dynamically: "D,s0,s1,...,sN,e0,e1,...,eM"
    char payload[64];
    int pos = appendChar(payload, 0, sizeof(payload), 'D');
    for (uint8_t i = 0; i < sensorCount && pos < (int)sizeof(payload) - 8; i++) {
        pos = appendChar(payload, pos, sizeof(payload), ',');
        pos = appendInt(payload, pos, sizeof(payload), (int)sensorValue(i));
    }
    for (uint8_t i = 0; i < eventCount && pos < (int)sizeof(payload) - 4; i++) {
        pos = appendChar(payload, pos, sizeof(payload), ',');
        pos = appendChar(payload, pos, sizeof(payload), eventState(i) ? '1' : '0');
    }
    sendPacket(payload);
}

void GreenhouseControlNode::sendSettingSync(const char* key, float value) {
    char payload[32];
    int pos = appendStr(payload, 0, sizeof(payload), "R,S,");
    pos = appendStr(payload, pos, sizeof(payload), key);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    appendFloat1(payload, pos, sizeof(payload), value);
    sendPacket(payload);
}

#ifndef DISABLE_NODE_ALERTS
void GreenhouseControlNode::sendSensorAlertSync(uint8_t sensorIndex) {
    if (sensorIndex >= sensorCount) return;

    char payload[48];
    const SensorAlertConfig& alert = sensorAlerts[sensorIndex];
    int pos = appendStr(payload, 0, sizeof(payload), "R,A,");
    pos = appendInt(payload, pos, sizeof(payload), sensorIndex);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    pos = appendInt(payload, pos, sizeof(payload), alert.lowEnabled ? 1 : 0);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    pos = appendFloat1(payload, pos, sizeof(payload), alert.lowThreshold);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    pos = appendInt(payload, pos, sizeof(payload), alert.highEnabled ? 1 : 0);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    appendFloat1(payload, pos, sizeof(payload), alert.highThreshold);
    sendPacket(payload);
}

void GreenhouseControlNode::sendEventAlertSync(uint8_t eventIndex) {
    if (eventIndex >= eventCount) return;

    char payload[32];
    int pos = appendStr(payload, 0, sizeof(payload), "R,E,");
    pos = appendInt(payload, pos, sizeof(payload), eventIndex);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    appendInt(payload, pos, sizeof(payload), eventAlerts[eventIndex].enabled ? 1 : 0);
    sendPacket(payload);
}
#endif

void GreenhouseControlNode::sendFullStateSync() {
    for (uint8_t i = 0; i < settingCount; i++) {
        if (!settings[i].key || !settings[i].valueRef) continue;
        sendSettingSync(settings[i].key, *(settings[i].valueRef));
    }

#ifndef DISABLE_NODE_ALERTS
    for (uint8_t i = 0; i < sensorCount; i++) {
        sendSensorAlertSync(i);
    }
    for (uint8_t i = 0; i < eventCount; i++) {
        sendEventAlertSync(i);
    }
#endif
}

#ifndef DISABLE_NODE_ALERTS
void GreenhouseControlNode::evaluateSensorAlerts() {
    for (uint8_t i = 0; i < sensorCount; i++) {
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
    char payload[48];
    int pos = appendStr(payload, 0, sizeof(payload), "N,S,");
    pos = appendInt(payload, pos, sizeof(payload), sensorIndex);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    pos = appendChar(payload, pos, sizeof(payload), direction);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    pos = appendFloat1(payload, pos, sizeof(payload), currentValue);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    appendFloat1(payload, pos, sizeof(payload), threshold);
    sendPacket(payload);
}
#endif

void GreenhouseControlNode::setEventState(uint8_t eventIndex, bool newState, bool shouldNotify) {
    if (eventIndex >= eventCount || !events || !events[eventIndex].stateRef) return;

#ifndef DISABLE_NODE_ALERTS
    bool previousState = *(events[eventIndex].stateRef);
#endif
    *(events[eventIndex].stateRef) = newState;
    if (events[eventIndex].onSetState) {
        events[eventIndex].onSetState(newState);
    }

#ifndef DISABLE_NODE_ALERTS
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
#else
    (void)shouldNotify;
#endif
}

#ifndef DISABLE_NODE_ALERTS
void GreenhouseControlNode::sendEventAlertNotification(uint8_t eventIndex, bool state) {
    char payload[16];
    int pos = appendStr(payload, 0, sizeof(payload), "N,E,");
    pos = appendInt(payload, pos, sizeof(payload), eventIndex);
    pos = appendChar(payload, pos, sizeof(payload), ',');
    appendChar(payload, pos, sizeof(payload), state ? '1' : '0');
    sendPacket(payload);
}
#endif

float GreenhouseControlNode::sensorValue(uint8_t sensorIndex) const {
    if (!sensors || sensorIndex >= sensorCount || !sensors[sensorIndex].valueRef) return 0.0f;
    return (float)(*(sensors[sensorIndex].valueRef));
}

bool GreenhouseControlNode::eventState(uint8_t eventIndex) const {
    if (!events || eventIndex >= eventCount || !events[eventIndex].stateRef) return false;
    return *(events[eventIndex].stateRef);
}

int GreenhouseControlNode::findEventIndexByKey(const char* key) const {
    if (!key || !events) return -1;
    for (uint8_t i = 0; i < eventCount; i++) {
        if (events[i].key && strcmp(events[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

int GreenhouseControlNode::findSettingIndexByKey(const char* key) const {
    if (!key || !settings) return -1;
    for (uint8_t i = 0; i < settingCount; i++) {
        if (settings[i].key && strcmp(settings[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

bool GreenhouseControlNode::handleControlCommand(const char* deviceKey, int value) {
    int eventIndex = findEventIndexByKey(deviceKey);
    if (eventIndex < 0) return false;

    bool state = value == 1;
    setEventState((uint8_t)eventIndex, state, true);
    return true;
}

bool GreenhouseControlNode::handleSettingCommand(const char* key, float value) {
    int settingIndex = findSettingIndexByKey(key);
    if (settingIndex < 0 || !settings[settingIndex].valueRef) return false;

    *(settings[settingIndex].valueRef) = value;
    saveSettingValue(key, value);
    sendSettingSync(key, value);
    return true;
}

#ifndef DISABLE_NODE_ALERTS
void GreenhouseControlNode::handleSensorAlertCommand(int sensorIndex, char direction, int enabled, float threshold) {
    if (sensorIndex < 0 || sensorIndex >= sensorCount) return;

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
    if (eventIndex < 0 || eventIndex >= eventCount) return;

    eventAlerts[eventIndex].enabled = enabled == 1;
    eventAlerts[eventIndex].lastStateKnown = true;
    eventAlerts[eventIndex].lastState = eventState(eventIndex);
    saveEventAlert(eventIndex);
    sendEventAlertSync(eventIndex);
}
#endif
