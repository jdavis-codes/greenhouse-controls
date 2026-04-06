#include <Arduino.h>
#include "RYLR_LoRaAT.h"

// guide for loara library usage:
// https://medium.com/@jmwanderer/writing-an-arduino-library-for-a-uart-based-lora-device-9a3e42e91e94

// Serial1 pins for the RYLR radio (adjust for your wiring)
#define LORA_RX RX
#define LORA_TX TX

// Address of this sender and the forwarder we're sending to
#define LOCAL_ADDRESS 1
#define REMOTE_ADDRESS 2

RYLR_LoRaAT rylr;
unsigned long activityBlinkUntil = 0;

// Dummy sensor data
int temp = 82;       // degrees F
int humidity = 58;   // %rH
int moisture = 45;   // soil moisture 0-100
int insolation = 73; // light level 0-100
bool motor_up = false;
bool fan_on = false;

// Send interval
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 15000; // 15 seconds
const unsigned long ACTIVITY_BLINK_MS = 120;

void writeRgbLed(bool redOn, bool greenOn, bool blueOn)
{
    digitalWrite(LEDR, redOn ? LOW : HIGH);
    digitalWrite(LEDG, greenOn ? LOW : HIGH);
    digitalWrite(LEDB, blueOn ? LOW : HIGH);
}

void showRoleLedRED()
{
    // set red on for sender role
    writeRgbLed(false, false, true);
}

void blinkActivityLed()
{
    activityBlinkUntil = millis() + ACTIVITY_BLINK_MS;
    writeRgbLed(false, true, false);
}

void updateStatusLed()
{
    if (millis() >= activityBlinkUntil)
    {
        showRoleLedRED();
    }
}

void updateDummySensorData(unsigned long now)
{
    temp = 78 + (now / 10000) % 15;      // 78-92 F
    humidity = 50 + (now / 8000) % 20;   // 50-69 %
    moisture = 30 + (now / 12000) % 40;  // 30-69
    insolation = 20 + (now / 6000) % 60; // 20-79
    motor_up = (temp >= 84);
    fan_on = (temp >= 89);
}

void sendSensorPayload()
{
    char payload[64];
    snprintf(payload, sizeof(payload), "%d,%d,%d,%d,%d,%d",
             temp, humidity, moisture, insolation,
             motor_up ? 1 : 0, fan_on ? 1 : 0);

    Serial.printf("TX -> [%d]: %s\n", REMOTE_ADDRESS, payload);

    rylr.startTxMessage();
    rylr.addTxData(payload);
    int result = rylr.sendTxMessage(REMOTE_ADDRESS);
    blinkActivityLed();
    Serial.printf("Send result: %d\n", result);
}

void receiveReplies()
{
    RYLR_LoRaAT_Message *message = rylr.checkMessage();
    if (message)
    {
        blinkActivityLed();
        Serial.printf("RX from %d (RSSI %d, SNR %d): %s\n",
                      message->from_address, message->rssi,
                      message->snr, message->data);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    pinMode(LEDR, OUTPUT);
    pinMode(LEDG, OUTPUT);
    pinMode(LEDB, OUTPUT);

    showRoleLedRED(); // indicate sender role with red LED

    // Start LoRa UART
    Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
    rylr.setSerial(&Serial1);

    int result = rylr.checkStatus();
    Serial.printf("LoRa status: %d\n", result);

    rylr.setAddress(LOCAL_ADDRESS);
    rylr.setRFPower(14);

    Serial.println("LoRa Sender ready. Sending dummy data...");
}

void loop()
{
    unsigned long now = millis();

    if (now - lastSend > SEND_INTERVAL)
    {
        lastSend = now;
        updateDummySensorData(now);
        sendSensorPayload();
    }

    receiveReplies();
    updateStatusLed();
}
