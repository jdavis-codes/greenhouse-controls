
// standard faire for Arduino sketches / esp32 wifi connection
#include <Arduino.h>
#include <WiFi.h>

// this is a simple library for printing messages directly to a telegram chat via WiFi, 
// without needing a full Telegram bot library or webhook setup
#include <TelegramSerial.h>

// guide for loara library usage:
// https://medium.com/@jmwanderer/writing-an-arduino-library-for-a-uart-based-lora-device-9a3e42e91e94
#include "RYLR_LoRaAT.h"

// secrets.h contains WiFi and Telegram bot credentials, which are not checked into source control
// so the internet does not peek at them!
#include "secrets.h"

// Serial1 pins for the RYLR radio (adjust for your wiring)
#define LORA_RX RX
#define LORA_TX TX

// this tg object talks to our telegram chat specified by ID here.
TelegramSerial tg(WIFI_SSID, WIFI_PASSWORD, BOT_TOKEN, PERSONAL_CHAT_ID, &Serial);

// LoRa library object
RYLR_LoRaAT rylr;

//global for activity LED blinking
unsigned long activityBlinkUntil = 0;
const unsigned long ACTIVITY_BLINK_MS = 120;

void writeRgbLed(bool redOn, bool greenOn, bool blueOn)
{
    digitalWrite(LEDR, redOn ? LOW : HIGH);
    digitalWrite(LEDG, greenOn ? LOW : HIGH);
    digitalWrite(LEDB, blueOn ? LOW : HIGH);
}

void showRoleLed()
{
    writeRgbLed(true, false, false);
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
        showRoleLed();
    }
}

void forwardRawMessage(const RYLR_LoRaAT_Message *message)
{
    tg.printf("📡 LoRa [addr %d | RSSI %d]: %s\n",
              message->from_address, message->rssi, message->data);
}

void forwardStructuredMessage(const RYLR_LoRaAT_Message *message)
{
    int temp, humidity, moisture, insolation, motor_up, fan_on;
    int parsed = sscanf(message->data, "%d,%d,%d,%d,%d,%d",
                        &temp, &humidity, &moisture, &insolation, &motor_up, &fan_on);

    if (parsed == 6)
    {
        tg.printf("🌡️ %dF  💧 %d%%  🌱 %d%%  ☀️ %d%%\n"
                  "🔄 Motor: %s  🌀 Fan: %s\n"
                  "📶 RSSI: %d  SNR: %d\n",
                  temp, humidity, moisture, insolation,
                  motor_up ? "UP" : "DOWN", fan_on ? "ON" : "OFF",
                  message->rssi, message->snr);
        return;
    }

    blinkActivityLed();
    forwardRawMessage(message);
}

void receiveAndForwardMessages()
{
    RYLR_LoRaAT_Message *message = rylr.checkMessage();
    if (!message)
    {
        return;
    }

    blinkActivityLed();
    Serial.printf("LoRa from %d (RSSI %d, SNR %d): %s\n",
                  message->from_address, message->rssi, message->snr, message->data);
    forwardStructuredMessage(message);
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    pinMode(LEDR, OUTPUT);
    pinMode(LEDG, OUTPUT);
    pinMode(LEDB, OUTPUT);
    showRoleLed();

    // Start LoRa UART and attach to the library
    Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
    rylr.setSerial(&Serial1);

    int result = rylr.checkStatus();
    Serial.printf("LoRa status: %d\n", result);

    // Set LoRa address (must match the sender's target address)
    rylr.setAddress(2);
    rylr.setRFPower(14);

    // Connect WiFi via TelegramSerial
    if (!tg.begin())
    {
        Serial.println("WiFi failed — messages will queue until connected");
    }
    else
    {
        Serial.println("WiFi connected!");
    }

    tg.println("📡 Telegram Forwarder online! Listening for LoRa messages...");
}

void loop()
{
    tg.update();
    receiveAndForwardMessages();
    updateStatusLed();
}
