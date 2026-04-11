#include <Arduino.h>
#include <WiFi.h>
#include "RYLR_LoRaAT.h"
#include "GreenhouseTelegram.h"
#include "secrets.h"
#include <time.h>
#include <RTClib.h>

// Forwarder pins for the RYLR radio
#define LORA_RX RX
#define LORA_TX TX
#define LOCAL_ADDRESS 2
#define REMOTE_ADDRESS 1

enum data_pipe {
    radio,
    uart_rx_tx
};

constexpr data_pipe selected_data_pipe = radio;

RYLR_LoRaAT rylr;
unsigned long activityBlinkUntil = 0;
const unsigned long ACTIVITY_BLINK_MS = 120;

RingBuffer* logBuffer = nullptr;
GreenhouseTelegramBot telBot(BOT_TOKEN, MODE_CHAT);

// Data state cache mostly for the active setting UI, though the UI relies heavily on log entry.
float grnhouseTargetTemp1 = 80.0;
float grnhouseTargetTemp2 = 85.0;
float grnhouseTempDelta = 4.0;
int soilTargetMoisture = 50;
int soilMoistureDelta = 10;

// Forward declare callbacks
void onControlCommandRequested(const char* key, bool state);
void onSensorAlertChanged(int sensorIdx, AlertThresholdType thresholdType, bool enabled, float threshold);
void onEventAlertChanged(int eventIdx, bool enabled);
void sendLoRaPayload(const char* payload);
void requestNodeSync();
const char* wifiStatusLabel(wl_status_t status);
const char* wifiEncryptionLabel(wifi_auth_mode_t encryption);
bool connectWiFiOrExplain();
void printWiFiTroubleshooting(wl_status_t status);
void scanAndListNearbyNetworks();
static bool readLineFromPipe(Stream& pipe, char* buffer, size_t bufferLen);

// activeSensors order MUST exactly match the sender's SensorBinding array order,
// since sensor alert indices [0..N] are shared over LoRa between both nodes.
SensorMetadata activeSensors[] = {
    {"Green House Temperature", "°F", "#ff4d4d", &LogEntry::grnhouseTemp},  // [0] GH_TEMP
    {"Green House Humidity",    "%",  "#007bff", &LogEntry::grnhouseHum},   // [1] GH_HUM
    {"Soil Moisture",          "%",  "#4dff4d", &LogEntry::soilMoisture},   // [2] SOIL
    {"Insolation",             "%",  "#ffff4d", &LogEntry::insolation},     // [3] SUN
    {"Ambient Temperature",    "°F", "#ffb54d", &LogEntry::ambientTemp},    // [4] AMB_TEMP
    {"Ambient Humidity",       "%",  "#87ceeb", &LogEntry::ambientHum}      // [5] AMB_HUM
};

EventMetadata activeEvents[] = {
    {"Exhaust Fan", "FAN", "🟩", "  ", "ON", "OFF", "#32cd32", &LogEntry::fanOn},
    {"Roller Sides", "SIDES", "🟧", "  ", "UP", "DOWN", "#ffa500", &LogEntry::motorUp},
    {"Irrigation", "WATER", "💧", "  ", "ON", "OFF", "#1e90ff", &LogEntry::waterOn}
};

SettingsParameter botSettings[] = {
    {"🌡️", "Target Temp 1", "TEMP1", &grnhouseTargetTemp1, 60.0, 100.0, "°F"},
    {"🌡️", "Target Temp 2", "TEMP2", &grnhouseTargetTemp2, 60.0, 120.0, "°F"},
    {"📈", "Temp Delta", "TDELTA", &grnhouseTempDelta, 0.5, 20.0, "°F"},
    {"💧", "Target Moisture", "MOIST", (float *)&soilTargetMoisture, 0.0, 100.0, "%"}, 
    {"📉", "Moisture Delta", "MDELTA", (float *)&soilMoistureDelta, 1.0, 50.0, "%"}
};

static bool readLineFromPipe(Stream& pipe, char* buffer, size_t bufferLen) {
    static size_t index = 0;

    while (pipe.available()) {
        int ch = pipe.read();
        if (ch < 0) break;

        if (ch == '\r') continue;
        if (ch == '\n') {
            if (index == 0) continue;
            buffer[index] = '\0';
            index = 0;
            return true;
        }

        if (index + 1 < bufferLen) {
            buffer[index++] = (char)ch;
        }
    }

    return false;
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(LEDR, OUTPUT);
    pinMode(LEDG, OUTPUT);
    pinMode(LEDB, OUTPUT);
    showRoleLedGREEN();

    switch (selected_data_pipe) {
        case radio: {
            Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
            rylr.setSerial(&Serial1);

            int result = rylr.checkStatus();
            Serial.printf("LoRa status: %d\n", result);

            rylr.setAddress(LOCAL_ADDRESS);
            rylr.setRFPower(14);
            Serial.println("Receiver using RADIO data pipe.");
            break;
        }
        case uart_rx_tx: {
            Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
            Serial.println("Receiver using UART RX/TX data pipe.");
            break;
        }
    }

    // Start WiFi
    if (!connectWiFiOrExplain()) {
        Serial.println("WiFi connection failed. Telegram forwarding will not start.");
        while (true) {
            blinkActivityLed();
            delay(250);
            updateStatusLed();
            delay(750);
        }
    }

    // Initialize telegram bot which handles WiFi and the PSRAM RingBuffer setup
    telBot.begin(activeSensors, activeEvents, botSettings);
    telBot.configureKnownChats(PERSONAL_CHAT_ID, GROUP_CHAT_ID);
    telBot.onSettingChanged(onSettingChanged);
    telBot.onSensorAlertChanged(onSensorAlertChanged);
    telBot.onEventAlertChanged(onEventAlertChanged);
    telBot.onControlCommand(onControlCommandRequested);
    logBuffer = telBot.getLogBuffer();
    telBot.sendBootAnnouncements();
    requestNodeSync();

    Serial.println("Greenhouse LoRa Telegram Forwarder ready.");
}

void loop() {
    telBot.tick();
    receiveAndProcessLoRa();
    updateStatusLed();
}

void processIncomingDataPacket(const RYLR_LoRaAT_Message *message) {
    if (strncmp(message->data, "D,", 2) != 0) return;

    const int numSensors = (int)(sizeof(activeSensors) / sizeof(activeSensors[0]));
    const int numEvents  = (int)(sizeof(activeEvents)  / sizeof(activeEvents[0]));
    const int expected   = numSensors + numEvents;

    // Parse all comma-separated integer values from the payload dynamically.
    // The packet format is: "D,s0,s1,...,sN,e0,e1,...,eM"
    // Sensor and event counts on this side MUST match the sender's binding arrays.
    int values[16] = {};
    int count = 0;
    const char* p = message->data + 2; // skip "D,"
    while (*p && count < expected) {
        char* end;
        values[count++] = (int)strtol(p, &end, 10);
        if (end == p) break; // no conversion -- malformed packet
        p = (*end == ',') ? end + 1 : end;
    }

    if (count != expected) {
        Serial.printf("LoRa parse mismatch, got %d values, expected %d\n", count, expected);
        return;
    }

    // Populate LogEntry using pointer-to-member -- no positional hardcoding.
    LogEntry newEntry;
    newEntry.timestamp = getUnixTime();
    for (int i = 0; i < numSensors; i++) {
        newEntry.*(activeSensors[i].valueField) = (float)values[i];
    }
    for (int i = 0; i < numEvents; i++) {
        newEntry.*(activeEvents[i].stateField) = values[numSensors + i] != 0;
    }

    if (logBuffer) {
        logBuffer->push_back(newEntry);
        telBot.evaluateAlerts();
    }

    Serial.printf("Data Logged -> Temp: %.0fF, Hum: %.0f%%, Soil: %.0f%%, Fan: %d, Sides: %d, Water: %d\n",
                  newEntry.grnhouseTemp, newEntry.grnhouseHum, newEntry.soilMoisture,
                  (int)newEntry.fanOn, (int)newEntry.motorUp, (int)newEntry.waterOn);

    telBot.refreshDashboard();
}

void processSyncPacket(const RYLR_LoRaAT_Message *message) {
    if (strncmp(message->data, "R,S,", 4) == 0) {
        char key[16];
        float value = 0.0f;
        if (sscanf(message->data, "R,S,%15[^,],%f", key, &value) == 2) {
            if (telBot.syncSettingValue(key, value)) {
                Serial.printf("Synced setting %s = %.1f\n", key, value);
            }
        }
        return;
    }

    if (strncmp(message->data, "R,A,", 4) == 0) {
        int sensorIdx = -1;
        int lowEnabled = 0;
        float lowThreshold = 0.0f;
        int highEnabled = 0;
        float highThreshold = 0.0f;
        if (sscanf(message->data, "R,A,%d,%d,%f,%d,%f", &sensorIdx, &lowEnabled, &lowThreshold, &highEnabled, &highThreshold) == 5) {
            telBot.syncSensorAlertConfig(sensorIdx, lowEnabled == 1, lowThreshold, highEnabled == 1, highThreshold);
            Serial.printf("Synced sensor alert %d\n", sensorIdx);
        }
        return;
    }

    if (strncmp(message->data, "R,E,", 4) == 0) {
        int eventIdx = -1;
        int enabled = 0;
        if (sscanf(message->data, "R,E,%d,%d", &eventIdx, &enabled) == 2) {
            telBot.syncEventAlertConfig(eventIdx, enabled == 1);
            Serial.printf("Synced event alert %d = %d\n", eventIdx, enabled);
        }
    }
}

void processAlertNotificationPacket(const RYLR_LoRaAT_Message *message) {
    if (strncmp(message->data, "N,S,", 4) == 0) {
        int sensorIdx = -1;
        char direction = '\0';
        float currentValue = 0.0f;
        float threshold = 0.0f;
        if (sscanf(message->data, "N,S,%d,%c,%f,%f", &sensorIdx, &direction, &currentValue, &threshold) == 4 && sensorIdx >= 0 && sensorIdx < (int)(sizeof(activeSensors) / sizeof(activeSensors[0]))) {
            String msg = direction == 'L' ? "🚨 <b>Low Alert</b>\n" : "🚨 <b>High Alert</b>\n";
            msg += activeSensors[sensorIdx].name;
            msg += direction == 'L' ? " dropped to <code>" : " rose to <code>";
            msg += String(currentValue, 1) + String(activeSensors[sensorIdx].unit) + "</code>\n";
            msg += "Threshold: <code>" + String(threshold, 1) + String(activeSensors[sensorIdx].unit) + "</code>";
            telBot.broadcastAlertMessage(msg, true);
        }
        return;
    }

    if (strncmp(message->data, "N,E,", 4) == 0) {
        int eventIdx = -1;
        int state = 0;
        if (sscanf(message->data, "N,E,%d,%d", &eventIdx, &state) == 2 && eventIdx >= 0 && eventIdx < (int)(sizeof(activeEvents) / sizeof(activeEvents[0]))) {
            String msg = "🔔 <b>Equipment Alert</b>\n";
            msg += activeEvents[eventIdx].name;
            msg += " is now <code>";
            msg += state == 1 ? activeEvents[eventIdx].onStr : activeEvents[eventIdx].offStr;
            msg += "</code>";
            telBot.broadcastAlertMessage(msg, true);
        }
    }
}

void receiveAndProcessLoRa() {
    switch (selected_data_pipe) {
        case radio: {
            RYLR_LoRaAT_Message *message = rylr.checkMessage();
            if (!message) return;

            blinkActivityLed();
            telBot.updateLinkMetrics(message->rssi, message->snr);
            Serial.printf("RX from %d (RSSI %d, SNR %d) [%d bytes]: %s\n",
                          message->from_address, message->rssi, message->snr, message->data_len, message->data);

            if (strncmp(message->data, "D,", 2) == 0) {
                processIncomingDataPacket(message);
                return;
            }
            if (strncmp(message->data, "R,", 2) == 0) {
                processSyncPacket(message);
                return;
            }
            if (strncmp(message->data, "N,", 2) == 0) {
                processAlertNotificationPacket(message);
            }
            break;
        }
        case uart_rx_tx: {
            char line[160];
            if (!readLineFromPipe(Serial1, line, sizeof(line))) return;

            blinkActivityLed();
            Serial.printf("RX UART: %s\n", line);

            RYLR_LoRaAT_Message pseudo = {};
            pseudo.from_address = 0;
            pseudo.rssi = 0;
            pseudo.snr = 0;
            pseudo.data = line;
            pseudo.data_len = strlen(line);

            if (strncmp(pseudo.data, "D,", 2) == 0) {
                processIncomingDataPacket(&pseudo);
                return;
            }
            if (strncmp(pseudo.data, "R,", 2) == 0) {
                processSyncPacket(&pseudo);
                return;
            }
            if (strncmp(pseudo.data, "N,", 2) == 0) {
                processAlertNotificationPacket(&pseudo);
            }
            break;
        }
    }
}

void onSettingChanged(const char* key, float value) {
    char payload[32];
    snprintf(payload, sizeof(payload), "S,%s,%.1f", key, value);
    sendLoRaPayload(payload);
}

// Callbacks that push commands back OUT over LoRa to the sender node
void onControlCommandRequested(const char* key, bool state) {
    if (!key || !key[0]) return;

    char payload[32];
    snprintf(payload, sizeof(payload), "C,%s,%d", key, state ? 1 : 0);
    sendLoRaPayload(payload);
}

void onSensorAlertChanged(int sensorIdx, AlertThresholdType thresholdType, bool enabled, float threshold) {
    // The alert tracking is now server-side. We no longer explicitly write rules OUT over LoRa.
    // Log it instead and rely on locally stored thresholds via Preferences.
    Serial.printf("Server-Side ALERTS: Updated threshold %c for %s to %.2f (st: %d)\n", 
                  thresholdType == ALERT_THRESHOLD_LOW ? 'L' : 'H',
                  activeSensors[sensorIdx].name, threshold, enabled);
}

void onEventAlertChanged(int eventIdx, bool enabled) {
    Serial.printf("Server-Side ALERTS: Updated event alert for %s (st: %d)\n", 
                  activeEvents[eventIdx].name, enabled);
}

void sendLoRaPayload(const char* payload) {
    switch (selected_data_pipe) {
        case radio:
            rylr.startTxMessage();
            rylr.addTxData(payload);
            rylr.sendTxMessage(REMOTE_ADDRESS);
            Serial.printf("LoRa TX: %s\n", payload);
            break;
        case uart_rx_tx:
            Serial1.println(payload);
            Serial.printf("UART TX: %s\n", payload);
            break;
    }
}

void requestNodeSync() {
    sendLoRaPayload("Q,SYNC");
}

const char* wifiStatusLabel(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS:
            return "idle";
        case WL_NO_SSID_AVAIL:
            return "ssid not found";
        case WL_SCAN_COMPLETED:
            return "scan completed";
        case WL_CONNECTED:
            return "connected";
        case WL_CONNECT_FAILED:
            return "connection failed";
        case WL_CONNECTION_LOST:
            return "connection lost";
        case WL_DISCONNECTED:
            return "disconnected";
        default:
            return "unknown";
    }
}

bool connectWiFiOrExplain() {
    const unsigned long connectTimeoutMs = 30000;
    const unsigned long statusPrintIntervalMs = 2000;
    unsigned long startedAt = millis();
    unsigned long lastStatusPrintAt = 0;
    wl_status_t lastStatus = WL_IDLE_STATUS;

    Serial.printf("Connecting to WiFi SSID '%s'...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    scanAndListNearbyNetworks();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (millis() - startedAt < connectTimeoutMs) {
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            Serial.println("WiFi connected.");
            Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
            return true;
        }

        unsigned long now = millis();
        if (status != lastStatus || now - lastStatusPrintAt >= statusPrintIntervalMs) {
            Serial.printf("  Waiting... status=%s (%d), elapsed=%lus\n",
                          wifiStatusLabel(status),
                          (int)status,
                          (now - startedAt) / 1000);
            lastStatus = status;
            lastStatusPrintAt = now;
        }

        delay(250);
    }

    wl_status_t finalStatus = WiFi.status();
    Serial.printf("WiFi connection timed out after %lus. Final status: %s (%d)\n",
                  connectTimeoutMs / 1000,
                  wifiStatusLabel(finalStatus),
                  (int)finalStatus);
    printWiFiTroubleshooting(finalStatus);
    return false;
}

const char* wifiEncryptionLabel(wifi_auth_mode_t encryption) {
    switch (encryption) {
        case WIFI_AUTH_OPEN:
            return "open";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA+WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2-EAP";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2+WPA3";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI";
        default:
            return "unknown";
    }
}

void printWiFiTroubleshooting(wl_status_t status) {
    Serial.println("Things to try:");
    Serial.println("  1. Confirm WIFI_SSID and WIFI_PASSWORD in secrets.h are correct.");
    Serial.println("  2. Make sure the network is 2.4 GHz and the ESP32 is in range.");
    Serial.println("  3. Power-cycle the board and router if the network was recently changed.");
    Serial.println("  4. Check whether another device can join the same SSID.");

    if (status == WL_NO_SSID_AVAIL) {
        Serial.println("Specific hint: the configured SSID was not found. Check spelling and whether the AP is broadcasting.");
    } else if (status == WL_CONNECT_FAILED) {
        Serial.println("Specific hint: association failed. Double-check the password and WiFi security mode.");
    } else if (status == WL_CONNECTION_LOST) {
        Serial.println("Specific hint: the board connected briefly, then lost the link. Check power stability and signal strength.");
    } else if (status == WL_DISCONNECTED || status == WL_IDLE_STATUS) {
        Serial.println("Specific hint: the board never completed association. Retry nearby the access point and confirm the router allows new clients.");
    }
}

void scanAndListNearbyNetworks() {
    Serial.println("Scanning for nearby WiFi networks...");
    int networkCount = WiFi.scanNetworks();

    if (networkCount < 0) {
        Serial.println("WiFi scan failed.");
        return;
    }

    if (networkCount == 0) {
        Serial.println("No WiFi networks found.");
        return;
    }

    Serial.printf("Found %d network%s:\n", networkCount, networkCount == 1 ? "" : "s");
    Serial.println("Nr | SSID                             | RSSI | CH | Encryption");
    for (int i = 0; i < networkCount; i++) {
        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);
        wifi_auth_mode_t encryption = WiFi.encryptionType(i);

        Serial.printf("%2d | %-32.32s | %4ld | %2d | %-10s%s\n",
                      i + 1,
                      ssid.length() ? ssid.c_str() : "<hidden>",
                      (long)rssi,
                      WiFi.channel(i),
                      wifiEncryptionLabel(encryption),
                      ssid == WIFI_SSID ? "  <-- target name match" : "");
    }

    WiFi.scanDelete();
}

void showRoleLedGREEN() {
    digitalWrite(LEDR, HIGH);
    digitalWrite(LEDG, LOW);
    digitalWrite(LEDB, HIGH);
}

void blinkActivityLed() {
    activityBlinkUntil = millis() + ACTIVITY_BLINK_MS;
    digitalWrite(LEDR, HIGH);
    digitalWrite(LEDG, HIGH);
    digitalWrite(LEDB, LOW); // Blink blue
}

void updateStatusLed() {
    if (millis() >= activityBlinkUntil) {
        showRoleLedGREEN();
    }
}

uint32_t getUnixTime() {
    // FastBot automatically synchronizes with NTP servers during operation.
    // If not connected to WiFi, we fall back to elapsed millis relative to an arbitrary boot time 
    // OR whatever hardware RTC time is. The RTClib's DateTime class provides nice formatting 
    // options entirely in software if initialized with a uint32_t unix timestamp.
    time_t now;
    time(&now);
    return (uint32_t)now;
}
