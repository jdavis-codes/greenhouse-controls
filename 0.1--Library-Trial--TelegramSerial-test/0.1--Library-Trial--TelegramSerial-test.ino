#include <Arduino.h>
#include <WiFi.h>

// Include the libraries you want to test:
#include <TelegramSerial.h>
// #include <FastBot2.h>
#include "secrets.h"

TelegramSerial tg(WIFI_SSID, WIFI_PASSWORD, BOT_TOKEN, PERSONAL_CHAT_ID, &Serial);

void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println("Starting TelegramSerial test...");

  if (!tg.begin())
  {
    Serial.println("WiFi failed — messages will queue until connected");
  }
  else
  {
    Serial.println("WiFi connected via TelegramSerial!");
  }
  // print secrets to serial monitor for verification
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("Password: ");
  Serial.println(WIFI_PASSWORD);
  Serial.print("Bot Token: ");
  Serial.println(BOT_TOKEN);
  Serial.print("Personal Chat ID: ");
  Serial.println(PERSONAL_CHAT_ID);
  Serial.print("Group Chat ID: ");
  Serial.println(GROUP_CHAT_ID);

  // Works exactly like Serial
  tg.println("🌱 ESP32 online in Test Environment!");
  tg.printf("🔧 Chip: %s  Rev: %d\n", ESP.getChipModel(), ESP.getChipRevision());
}

void loop()
{
  tg.update(); // drain send queue

  static unsigned long lastReport = 0;
  if (millis() - lastReport > 30000)
  {
    // tg.printf("Test Env Heap: %u bytes free\n", ESP.getFreeHeap());
    // fake temperature, humidity, and roller motor message for testing
    tg.printf("🌡️ Greenhouse Bot here! High temp alert: 85F, 💧 Humidity: 60%%, 🔄 Roller Motor: UP\n");
    lastReport = millis();
  }
}
