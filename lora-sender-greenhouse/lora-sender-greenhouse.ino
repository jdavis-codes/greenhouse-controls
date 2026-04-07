#include <Arduino.h>
#include "RYLR_LoRaAT.h"

// Serial1 pins for the RYLR radio (adjust for your wiring)
#define LORA_RX RX
#define LORA_TX TX

// Address of this sender and the forwarder we're sending to
#define LOCAL_ADDRESS 1
#define REMOTE_ADDRESS 2

RYLR_LoRaAT rylr;
unsigned long activityBlinkUntil = 0;
const unsigned long ACTIVITY_BLINK_MS = 120;

// Dummy sensor data & state
int temp = 82;
int humidity = 58;
int moisture = 45;
int insolation = 73;
bool motor_up = false;
bool fan_on = false;
bool water_on = false;

// Send interval
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 15000;

void writeRgbLed(bool redOn, bool greenOn, bool blueOn) {
    digitalWrite(LEDR, redOn ? LOW : HIGH);
    digitalWrite(LEDG, greenOn ? LOW : HIGH);
    digitalWrite(LEDB, blueOn ? LOW : HIGH);
}

void showRoleLedRED() { writeRgbLed(false, false, true); }
void blinkActivityLed() { activityBlinkUntil = millis() + ACTIVITY_BLINK_MS; writeRgbLed(false, true, false); }
void updateStatusLed() { if (millis() >= activityBlinkUntil) showRoleLedRED(); }

void updateDummySensorData(unsigned long now) {
    temp = 78 + (now / 10000) % 15;
    humidity = 50 + (now / 8000) % 20;
    moisture = 30 + (now / 12000) % 40;
    insolation = 20 + (now / 6000) % 60;
}

void sendSensorPayload() {
    char payload[64];
    // D,Temp,Humidity,Moisture,Insolation,Fan,Sides,Water
    // The Reyax RYLR module implicitly checks hardware level CRCs on receipt.
    // If the module emits "+RCV" over uart, the CRC was valid and SNR >= threshold.
    snprintf(payload, sizeof(payload), "D,%d,%d,%d,%d,%d,%d,%d",
             temp, humidity, moisture, insolation,
             fan_on ? 1 : 0, motor_up ? 1 : 0, water_on ? 1 : 0);

    Serial.printf("TX -> [%d]: %s\n", REMOTE_ADDRESS, payload);

    rylr.startTxMessage();
    rylr.addTxData(payload);
    int result = rylr.sendTxMessage(REMOTE_ADDRESS);
    blinkActivityLed();
    Serial.printf("Send result: %d\n", result);
}

void processCommand(const char* data) {
    // Expected command format: C,FAN,1 or C,SIDES,0
    char cmdType[16];
    int val = 0;
    if (sscanf(data, "C,%[^,],%d", cmdType, &val) == 2) {
        if (strcmp(cmdType, "FAN") == 0) {
            fan_on = (val == 1);
            Serial.printf("\n==================================\n");
            Serial.printf("📡 STUB EXEC: Fan set %s via LoRa\n", fan_on ? "ON" : "OFF");
            Serial.printf("==================================\n\n");
        } else if (strcmp(cmdType, "SIDES") == 0) {
            motor_up = (val == 1);
            Serial.printf("\n==================================\n");
            Serial.printf("📡 STUB EXEC: Sides set %s via LoRa\n", motor_up ? "UP" : "DOWN");
            Serial.printf("==================================\n\n");
        } else if (strcmp(cmdType, "WATER") == 0) {
            water_on = (val == 1);
            Serial.printf("\n==================================\n");
            Serial.printf("📡 STUB EXEC: Irrigation set %s via LoRa\n", water_on ? "ON" : "OFF");
            Serial.printf("==================================\n\n");
        }
    }
}

void receiveReplies() {
    RYLR_LoRaAT_Message *message = rylr.checkMessage();
    if (message) {
        blinkActivityLed();
        Serial.printf("RX from %d (RSSI %d, SNR %d) [%d bytes]: %s\n",
                      message->from_address, message->rssi,
                      message->snr, message->data_len, message->data);
        
        if (strncmp(message->data, "C,", 2) == 0) {
            processCommand(message->data);
            // Reply with updated state immediately
            sendSensorPayload();
            // Reset interval timer appropriately
            lastSend = millis();
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(LEDR, OUTPUT);
    pinMode(LEDG, OUTPUT);
    pinMode(LEDB, OUTPUT);
    showRoleLedRED();

    Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
    rylr.setSerial(&Serial1);

    int result = rylr.checkStatus();
    Serial.printf("LoRa status: %d\n", result);

    rylr.setAddress(LOCAL_ADDRESS);
    rylr.setRFPower(14);
    Serial.println("Greenhouse LoRa Sender node ready.");
}

void loop() {
    unsigned long now = millis();
    if (now - lastSend > SEND_INTERVAL) {
        lastSend = now;
        updateDummySensorData(now);
        sendSensorPayload();
    }
    receiveReplies();
    updateStatusLed();
}
