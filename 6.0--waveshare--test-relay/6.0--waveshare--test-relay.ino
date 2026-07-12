#include <Wire.h>
#include "WS_Relay.h"
#include "WS_GPIO.h"
#include "I2C_Driver.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // The ESP32-S3 needs to know the correct I2C pins for this board
  I2C_Init();
  
  // Initialize Buzzer and RGB LED so the Relay error task doesn't crash
  GPIO_Init();

  // Initializes TCA9554PWR and starts the background RelayFailTask
  Relay_Init();
  
  Serial.println("Relays Initialized. Starting cycling test...");
  
}

void loop() {
  // Cycle through pins 1 to 8 
  for (uint8_t pin = 1; pin <= 8; pin++) {
    Serial.print("Turning ON relay ");
    Serial.println(pin);
    Relay_Open(pin);
    delay(500);
    
    Serial.print("Turning OFF relay ");
    Serial.println(pin);
    Relay_Close(pin); // Note: The library spells it 'Closs'
    delay(500);
  }
}