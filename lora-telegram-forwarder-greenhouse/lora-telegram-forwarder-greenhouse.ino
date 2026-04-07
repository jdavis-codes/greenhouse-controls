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

RYLR_LoRaAT rylr;
unsigned long activityBlinkUntil = 0;
const unsigned long ACTIVITY_BLINK_MS = 120;

RingBuffer* logBuffer = nullptr;
GreenhouseTelegramBot telBot(BOT_TOKEN, MODE_DASHBOARD);

// Data state cache mostly for the active setting UI, though the UI relies heavily on log entry.
float grnhouseTargetTemp1 = 80.0;
float grnhouseTargetTemp2 = 85.0;
float grnhouseTempDelta = 4.0;
int soilTargetMoisture = 50;
int soilMoistureDelta = 10;

// Callbacks that push commands back OUT over LoRa to the sender node
void onFanTelegramTrigger(bool state) {
    char payload[32];
    snprintf(payload, sizeof(payload), "C,FAN,%d", state ? 1 : 0);
    rylr.startTxMessage();
    rylr.addTxData(payload);
    rylr.sendTxMessage(REMOTE_ADDRESS);
    Serial.printf("LoRa TX Command: %s\n", payload);
}

void onSidesTelegramTrigger(bool state) {
    char payload[32];
    snprintf(payload, sizeof(payload), "C,SIDES,%d", state ? 1 : 0);
    rylr.startTxMessage();
    rylr.addTxData(payload);
    rylr.sendTxMessage(REMOTE_ADDRESS);
    Serial.printf("LoRa TX Command: %s\n", payload);
}

void onIrrigationTelegramTrigger(bool state) {
    char payload[32];
    snprintf(payload, sizeof(payload), "C,WATER,%d", state ? 1 : 0);
    rylr.startTxMessage();
    rylr.addTxData(payload);
    rylr.sendTxMessage(REMOTE_ADDRESS);
    Serial.printf("LoRa TX Command: %s\n", payload);
}

// Map settings mirroring the greenhouse controller script
SensorMetadata activeSensors[] = {
    {"Green House Temperature", "°F", "#ff4d4d", &LogEntry::grnhouseTemp},
    {"Ambient Temperature", "°F", "#ffb54d", &LogEntry::ambientTemp},
    {"Green House Humidity", "%", "#007bff", &LogEntry::grnhouseHum},
    {"Insolation", "%", "#ffff4d", &LogEntry::insolation},
    {"Soil Moisture", "%", "#4dff4d", &LogEntry::soilMoisture}
};

EventMetadata activeEvents[] = {
    {"Exhaust Fan", "🟩", "  ", "ON", "OFF", "#32cd32", &LogEntry::fanOn, onFanTelegramTrigger},
    {"Roller Sides", "🟧", "  ", "UP", "DOWN", "#ffa500", &LogEntry::motorUp, onSidesTelegramTrigger},
    {"Irrigation", "💧", "  ", "ON", "OFF", "#1e90ff", &LogEntry::waterOn, onIrrigationTelegramTrigger}
};

SettingsParameter botSettings[] = {
    {"🌡️", "Target Temp 1", "TEMP1", &grnhouseTargetTemp1, "°F"},
    {"🌡️", "Target Temp 2", "TEMP2", &grnhouseTargetTemp2, "°F"},
    {"📈", "Temp Delta", "TDELTA", &grnhouseTempDelta, "°F"},
    {"💧", "Target Moisture", "MOIST", (float *)&soilTargetMoisture, "%"}, 
    {"📉", "Moisture Delta", "MDELTA", (float *)&soilMoistureDelta, "%"}
};

void onSettingChanged(const char* key, float value) {
    char payload[32];
    snprintf(payload, sizeof(payload), "S,%s,%.1f", key, value);
    rylr.startTxMessage();
    rylr.addTxData(payload);
    rylr.sendTxMessage(REMOTE_ADDRESS);
    Serial.printf("LoRa TX Setting: %s\n", payload);
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

void processIncomingData(const RYLR_LoRaAT_Message *message) {
    if (strncmp(message->data, "D,", 2) != 0) return; // Not a data packet

    int temp = 0, humidity = 0, moisture = 0, insolation = 0;
    int fan_on = 0, motor_up = 0, water_on = 0;

    int parsed = sscanf(message->data, "D,%d,%d,%d,%d,%d,%d,%d",
                        &temp, &humidity, &moisture, &insolation,
                        &fan_on, &motor_up, &water_on);

    if (parsed == 7) {
        LogEntry newEntry;
        newEntry.timestamp = getUnixTime();
        newEntry.grnhouseTemp = temp;
        newEntry.grnhouseHum = humidity;
        newEntry.soilMoisture = moisture;
        newEntry.insolation = insolation;
        newEntry.ambientTemp = temp - 5; // Fake ambient offset
        newEntry.ambientHum = humidity;
        newEntry.fanOn = fan_on;
        newEntry.motorUp = motor_up;
        newEntry.waterOn = water_on;
        
        if (logBuffer) {
            logBuffer->push_back(newEntry);
        }

        Serial.printf("Data Logged -> Temp: %dF, Fan: %d, Sides: %d, Water: %d\n", 
                      temp, fan_on, motor_up, water_on);
        
        telBot.refreshDashboard();
    } else {
        Serial.printf("LoRa parse mismatch, parsed: %d\n", parsed);
    }
}

void receiveAndProcessLoRa() {
    RYLR_LoRaAT_Message *message = rylr.checkMessage();
    if (!message) return;

    blinkActivityLed();
    telBot.updateLinkMetrics(message->rssi, message->snr);
    Serial.printf("RX from %d (RSSI %d, SNR %d) [%d bytes]: %s\n",
                  message->from_address, message->rssi, message->snr, message->data_len, message->data);
    
    processIncomingData(message);
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(LEDR, OUTPUT);
    pinMode(LEDG, OUTPUT);
    pinMode(LEDB, OUTPUT);
    showRoleLedGREEN();

    // Start LoRa UART
    Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
    rylr.setSerial(&Serial1);

    int result = rylr.checkStatus();
    Serial.printf("LoRa status: %d\n", result);

    rylr.setAddress(LOCAL_ADDRESS);
    rylr.setRFPower(14);

    // Initialize telegram bot which handles WiFi and the PSRAM RingBuffer setup
    telBot.begin(WIFI_SSID, WIFI_PASSWORD, activeSensors, activeEvents, botSettings);
    telBot.onSettingChanged(onSettingChanged);
    logBuffer = telBot.getLogBuffer();

    Serial.println("Greenhouse LoRa Telegram Forwarder ready.");
}

void loop() {
    telBot.tick();
    receiveAndProcessLoRa();
    updateStatusLed();
}
