#include <Arduino.h>
#include "ModbusSHT20.h"

#define RX_PIN 18
#define TX_PIN 17

ModbusSHT20 modbusSht20(RX_PIN, TX_PIN);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("Starting Modbus RS485 SHT20 Test...");

  // Initialize Modbus
  if (!modbusSht20.begin(9600)) {
    Serial.println("Failed to start Modbus RTU Client!");
    while (1);
  }

  Serial.println("Modbus started.");

  // For testing, run scans first
  Serial.println("--- Starting Scans ---");
  modbusSht20.scanSlaveIds();
  
  // Try scanning registers for default slave ID 1
  modbusSht20.scanRegisters(1);
  Serial.println("--- Scans Complete ---");
}

void loop() {
  float temp, hum;
  
  // Ask slave ID 1 for data
  if (modbusSht20.readSensor(1, temp, hum)) {
    Serial.print("Temperature: ");
    Serial.print(temp);
    Serial.println(" °C");
    
    Serial.print("Humidity: ");
    Serial.print(hum);
    Serial.println(" %");
  } else {
    Serial.println("Failed to read from sensor!");
  }

  delay(5000);
}