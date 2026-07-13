#ifndef MODBUS_SCANNER_H
#define MODBUS_SCANNER_H

#include <Arduino.h>
#include <ArduinoModbus.h>

class ModbusScanner {
public:
  // Scan specific slave IDs and try reading some common holding/input registers
  static void scanSlaveIds(int startSlaveId = 1, int endSlaveId = 247);

  // Scan a range of registers for a specific slave ID
  static void scanRegisters(int slaveId, int startReg = 0, int endReg = 10, bool scanCoils = true, bool scanDiscreteInputs = true, bool scanHoldingRegisters = true, bool scanInputRegisters = true);

  // Scan network for all devices, and automatically scan their registers if found
  static void scanAll(int startSlaveId = 1, int endSlaveId = 247, int startReg = 0, int endReg = 20, unsigned long timeoutMs = 200, bool scanCoils = true, bool scanDiscreteInputs = true, bool scanHoldingRegisters = true, bool scanInputRegisters = true);
};

#endif