#include <Arduino.h>
#include <SoftwareSerial.h>
#include "RYLR_LoRaAT_Software_Serial.h"

// ---- DIAGNOSTIC MODE ----
// Set to 1 to hold LORA_TX pin HIGH and stop.
// Measure voltage on the RYLR RX side of the level shifter — expect ~3.3V.
#define TX_PIN_TEST_MODE false

// SoftwareSerial pins for the RYLR radio (same wiring as field-ready sketch)
#define LORA_RX 5
#define LORA_TX 6

// Address of this sender and the forwarder we're sending to
#define LOCAL_ADDRESS  1
#define REMOTE_ADDRESS 2

SoftwareSerial radioSerial(LORA_RX, LORA_TX);
RYLR_LoRaAT_Software_Serial rylr;

// Built-in LED for activity blink
#define STATUS_LED LED_BUILTIN
unsigned long activityBlinkUntil = 0;
const unsigned long ACTIVITY_BLINK_MS = 120;

// Dummy sensor data
int temp = 82;
int humidity = 58;
int moisture = 45;
int insolation = 73;
bool motor_up = false;
bool fan_on = false;

// Send interval
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 1000; // 1 seconds

void blinkActivityLed() {
    activityBlinkUntil = millis() + ACTIVITY_BLINK_MS;
    digitalWrite(STATUS_LED, HIGH);
}

void updateStatusLed() {
    if (millis() >= activityBlinkUntil) {
        digitalWrite(STATUS_LED, LOW);
    }
}

void updateDummySensorData(unsigned long now) {
    temp = 78 + (now / 10000) % 15;
    humidity = 50 + (now / 8000) % 20;
    moisture = 30 + (now / 12000) % 40;
    insolation = 20 + (now / 6000) % 60;
    motor_up = (temp >= 84);
    fan_on = (temp >= 89);
}

void sendSensorPayload() {
    char payload[64];
    snprintf(payload, sizeof(payload), "%d,%d,%d,%d,%d,%d",
             temp, humidity, moisture, insolation,
             motor_up ? 1 : 0, fan_on ? 1 : 0);

    Serial.print(F("TX -> ["));
    Serial.print(REMOTE_ADDRESS);
    Serial.print(F("]: "));
    Serial.println(payload);

    rylr.startTxMessage();
    rylr.addTxData(payload);
    int result = rylr.sendTxMessage(REMOTE_ADDRESS);
    blinkActivityLed();

    Serial.print(F("Send result: "));
    Serial.println(result);
}

void receiveReplies() {
    RYLR_LoRaAT_Software_Serial_Message *message = rylr.checkMessage();
    if (message) {
        blinkActivityLed();
        Serial.print(F("RX from "));
        Serial.print(message->from_address);
        Serial.print(F(" (RSSI "));
        Serial.print(message->rssi);
        Serial.print(F(", SNR "));
        Serial.print(message->snr);
        Serial.print(F("): "));
        Serial.println(message->data);
    }
}

void setup() {
    Serial.begin(9600);
    delay(2000);

    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);

    Serial.println(F("=== Nano LoRa Radio Test ==="));

    if (TX_PIN_TEST_MODE){
        Serial.println(F("** TX PIN TEST MODE **"));
        Serial.print(F("Holding D"));
        Serial.print(LORA_TX);
        Serial.println(F(" HIGH — measure level-shifter output."));
        Serial.println(F("Expect ~3.3V on RYLR RX pin."));
        pinMode(LORA_TX, OUTPUT);
        digitalWrite(LORA_TX, HIGH);
        while (true) {
            digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
            delay(500);
        }
    }

    // --- Try 9600 first (preferred for SoftwareSerial) ---
    Serial.println(F("Trying radio at 9600..."));
    radioSerial.begin(9600);
    rylr.setSerial(&radioSerial);

    int status = rylr.checkStatus();
    Serial.print(F("Status @9600: "));
    Serial.println(status);

    if (status != 0) {
        // --- Try 115200 as fallback ---
        Serial.println(F("Trying radio at 115200..."));
        radioSerial.end();
        radioSerial.begin(115200);
        status = rylr.checkStatus();
        Serial.print(F("Status @115200: "));
        Serial.println(status);

        if (status == 0) {
            // Radio is at 115200 — reconfigure it to 9600
            Serial.println(F("Setting baud to 9600..."));
            radioSerial.print(F("AT+IPR=9600\r\n"));
            delay(500);
            radioSerial.end();
            radioSerial.begin(9600);
            status = rylr.checkStatus();
            Serial.print(F("Status @9600 after switch: "));
            Serial.println(status);
        }
    }

    if (status != 0) {
        Serial.println(F("*** RADIO NOT RESPONDING ***"));
        Serial.println(F("Check wiring: Nano D5->RYLR TX, Nano D6->RYLR RX"));
        Serial.println(F("Check power: RYLR needs 3.3V"));
        Serial.println(F("Continuing anyway to show send attempts..."));
    } else {
        Serial.println(F("Radio OK!"));
    }

    rylr.setAddress(LOCAL_ADDRESS);
    rylr.setRFPower(14);

    Serial.println(F("LoRa Sender ready. Sending dummy data..."));
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
