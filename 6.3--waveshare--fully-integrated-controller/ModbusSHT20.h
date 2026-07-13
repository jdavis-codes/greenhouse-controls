#ifndef MODBUS_SHT20_H
#define MODBUS_SHT20_H

#include <Arduino.h>
#include <ModbusMaster.h>  // Install: "ModbusMaster" by Doc Walker (4-20mA.com)

class ModbusSHT20 {
public:
  ModbusSHT20(int rxPin, int txPin);
  bool begin(long baudrate = 9600);

  // Read temperature and humidity from the sensor at the given Modbus slave ID.
  // Sensor registers: 0x0001 = temperature x10, 0x0002 = humidity x10
  bool readSensor(int slaveId, float &temperature, float &humidity);

private:
  int _rxPin;
  int _txPin;
  ModbusMaster _node;  // Handles Modbus RTU framing over Serial1
};

#endif
