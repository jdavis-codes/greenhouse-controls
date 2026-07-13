#ifndef MODBUS_SHT20_H
#define MODBUS_SHT20_H

#include <Arduino.h>
#include <ArduinoModbus.h>

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
  
  // Read temperature and humidity (assuming slave ID 1 and registers 0x01/0x02 by default, but we'll try 0x00, 0x01, 0x02 etc)
  bool readSensor(int slaveId, float &temperature, float &humidity);
  bool readConfig(int slaveId, Config &config);
  bool writeSlaveId(int currentSlaveId, uint16_t newSlaveId);
  bool writeBaudRate(int slaveId, uint16_t baudRate);
  bool writeConfigBlock(int currentSlaveId, const Config &config);
  bool writeTemperatureCompensation(int slaveId, float compensationC);
  bool writeHumidityCompensation(int slaveId, float compensationPercent);
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
};

#endif
