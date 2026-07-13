#include "ModbusSHT20.h"

ModbusSHT20::ModbusSHT20(int rxPin, int txPin) : _rxPin(rxPin), _txPin(txPin) {}

bool ModbusSHT20::begin(long baudrate) {
  // Map Serial1 to our RS485 pins (ESP32 lets you assign any UART to any pins)
  Serial1.begin(baudrate, SERIAL_8N1, _rxPin, _txPin);
  return true;
}

bool ModbusSHT20::readSensor(int slaveId, float &temperature, float &humidity) {
  // Tell the node which slave to talk to and which serial port to use
  _node.begin(slaveId, Serial1);

  // Read 2 input registers starting at 0x0001 (temp=0x0001, humidity=0x0002)
  // readInputRegisters() returns ku8MBSuccess (0x00) on success
  uint8_t result = _node.readInputRegisters(0x0001, 2);

  if (result == ModbusMaster::ku8MBSuccess) {
    // Sensor returns values x10 (e.g. 235 = 23.5°C), so divide by 10
    temperature = _node.getResponseBuffer(0) / 10.0;
    humidity    = _node.getResponseBuffer(1) / 10.0;
    return true;
  }

  return false;
}
