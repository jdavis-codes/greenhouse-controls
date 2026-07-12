#ifndef MODBUS_SHT20_H
#define MODBUS_SHT20_H

#include <Arduino.h>
#include <ArduinoModbus.h>

class ModbusSHT20 {
public:
  ModbusSHT20(int rxPin, int txPin);
  bool begin(long baudrate = 9600);
  
  // Read temperature and humidity (assuming slave ID 1 and registers 0x01/0x02 by default, but we'll try 0x00, 0x01, 0x02 etc)
  bool readSensor(int slaveId, float &temperature, float &humidity);

  // Scanning utilities
  void scanSlaveIds(long baudrate = 9600);
  void scanRegisters(int slaveId, long baudrate = 9600);

private:
  int _rxPin;
  int _txPin;
};

#endif
