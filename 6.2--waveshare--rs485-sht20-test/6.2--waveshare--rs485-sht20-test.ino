#include <Arduino.h>
#include "ModbusSHT20.h"
#include "ModbusScanner.h"

#define RX_PIN 18
#define TX_PIN 17

#define STARTING_SLAVE_ID 1
#define ENDING_SLAVE_ID 10

#define MAX_REGISTER 80

#define MODBUS_SENSOR_RESPONSE_TIMEOUT 500 // this seems to need to be greater than 
#define ACTIVE_SLAVE_ID 1

#define APPLY_SHT20_CONFIG_CHANGES true
#define TARGET_SLAVE_ID 2
#define TARGET_BAUD_RATE 14400
#define TARGET_TEMPERATURE_COMPENSATION_C 0.0
#define TARGET_HUMIDITY_COMPENSATION_PERCENT 0.0

ModbusSHT20 sht20(RX_PIN, TX_PIN);
int activeSlaveId = ACTIVE_SLAVE_ID;

void printConfig(int slaveId) {
  ModbusSHT20::Config config;
  if (!sht20.readConfig(slaveId, config)) {
    Serial.printf("Failed to read SHT20 config from Slave ID %d\n", slaveId);
    return;
  }

  Serial.printf("SHT20 config read via Slave ID %d:\n", slaveId);
  Serial.printf("  Slave ID register: %u\n", config.slaveId);
  Serial.printf("  Baud rate register: %u\n", config.baudRate);
  Serial.printf("  Temperature compensation: %.1f C\n", config.temperatureCompensationTenthsC / 10.0);
  Serial.printf("  Humidity compensation: %.1f %%RH\n", config.humidityCompensationTenthsPercent / 10.0);
}

void applyConfigChanges(int currentSlaveId) {
  if (!APPLY_SHT20_CONFIG_CHANGES) {
    return;
  }

  Serial.println("--- Applying SHT20 Config Changes ---");

  ModbusSHT20::Config targetConfig = {
    TARGET_SLAVE_ID,
    TARGET_BAUD_RATE,
    (int16_t)round(TARGET_TEMPERATURE_COMPENSATION_C * 10.0),
    (int16_t)round(TARGET_HUMIDITY_COMPENSATION_PERCENT * 10.0)
  };

  if (sht20.writeConfigBlock(currentSlaveId, targetConfig)) {
    Serial.printf("Sent full config write: ID %u, baud %u, temp %.1f C, humidity %.1f %%RH\n",
                  TARGET_SLAVE_ID,
                  TARGET_BAUD_RATE,
                  TARGET_TEMPERATURE_COMPENSATION_C,
                  TARGET_HUMIDITY_COMPENSATION_PERCENT);
  } else {
    Serial.printf("Failed full config write: %s\n", sht20.lastError());
  }

  ModbusSHT20::Config config;
  delay(200);
  if (sht20.readConfig(TARGET_SLAVE_ID, config)) {
    activeSlaveId = TARGET_SLAVE_ID;
    Serial.printf("Verified sensor responds at target Slave ID %d\n", activeSlaveId);
  } else {
    Serial.printf("Target Slave ID %u did not respond after config write: %s\n", TARGET_SLAVE_ID, sht20.lastError());
    if (sht20.readConfig(currentSlaveId, config)) {
      activeSlaveId = currentSlaveId;
      Serial.printf("Sensor still responds at old Slave ID %d; register value is %u\n", activeSlaveId, config.slaveId);
    } else {
      Serial.printf("Old Slave ID %d also did not respond: %s\n", currentSlaveId, sht20.lastError());
    }
  }

  Serial.println("--- SHT20 Config Changes Complete ---");
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("Starting Modbus RS485 SHT20 Test...");

  // Initialize Modbus
  if (!sht20.begin(9600)) {
    Serial.println("Failed to start Modbus RTU Client!");
    while (1);
  }

  Serial.println("Modbus started.");

  // For testing, run scans first
  Serial.println("--- Starting Scans ---");
  ModbusScanner::scanAll(STARTING_SLAVE_ID, ENDING_SLAVE_ID, 0, 260, MODBUS_SENSOR_RESPONSE_TIMEOUT,false, false, true, true);
  Serial.println("--- Scans Complete ---");

  printConfig(activeSlaveId);
  applyConfigChanges(activeSlaveId);
  printConfig(activeSlaveId);

}

void loop() {
  float temp, hum;
  
  if (sht20.readSensor(activeSlaveId, temp, hum)) {
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