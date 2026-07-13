#ifndef MODBUS_SHT20_H
#define MODBUS_SHT20_H

#include <Arduino.h>
#include <ModbusMaster.h>  // Install: "ModbusMaster" by Doc Walker (4-20mA.com)

class ModbusSHT20 {
public:
  struct Config {
    uint16_t slaveId;
    uint16_t baudRate;
    int16_t temperatureCompensationTenthsC;
    int16_t humidityCompensationTenthsPercent;
  };

  ModbusSHT20(int rxPin, int txPin);
  bool begin(long baudrate = 9600);

  // Read temperature and humidity from the sensor at the given Modbus slave ID.
  // Sensor registers: 0x0001 = temperature x10, 0x0002 = humidity x10
  bool readSensor(int slaveId, float &temperature, float &humidity);

  // Read/write the sensor's configuration registers (0x0101-0x0104)
  bool readConfig(int slaveId, Config &config);
  bool writeSlaveId(int currentSlaveId, uint16_t newSlaveId);
  bool writeBaudRate(int slaveId, uint16_t baudRate);
  bool writeConfigBlock(int currentSlaveId, const Config &config);
  bool writeTemperatureCompensation(int slaveId, float compensationC);
  bool writeHumidityCompensation(int slaveId, float compensationPercent);

  // Human-readable description of the last Modbus error
  const char *lastError();

private:
  static const uint16_t REG_SLAVE_ID = 0x0101;
  static const uint16_t REG_BAUD_RATE = 0x0102;
  static const uint16_t REG_TEMPERATURE_COMPENSATION = 0x0103;
  static const uint16_t REG_HUMIDITY_COMPENSATION = 0x0104;

  bool readHoldingRegister(int slaveId, uint16_t address, uint16_t &value);
  bool writeHoldingRegister(int slaveId, uint16_t address, uint16_t value);
  bool isValidBaudRate(uint16_t baudRate);
  static int16_t toSignedRegister(uint16_t value);
  static uint16_t fromSignedRegister(int16_t value);

  int _rxPin;
  int _txPin;
  ModbusMaster _node;     // Handles Modbus RTU framing over Serial1
  uint8_t _lastError;     // Last ModbusMaster result code
};

#endif