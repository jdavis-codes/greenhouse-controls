#include "ModbusSHT20.h"

// ── Human-readable error strings for ModbusMaster result codes ────────────────

static const char *modbusErrorString(uint8_t code) {
  switch (code) {
    case ModbusMaster::ku8MBSuccess:          return "Success";
    case ModbusMaster::ku8MBIllegalFunction:  return "Illegal function";
    case ModbusMaster::ku8MBIllegalDataAddress: return "Illegal data address";
    case ModbusMaster::ku8MBIllegalDataValue: return "Illegal data value";
    case ModbusMaster::ku8MBSlaveDeviceFailure: return "Slave device failure";
    case ModbusMaster::ku8MBInvalidSlaveID:   return "Invalid slave ID";
    case ModbusMaster::ku8MBInvalidFunction:  return "Invalid function";
    case ModbusMaster::ku8MBResponseTimedOut: return "Response timed out";
    case ModbusMaster::ku8MBInvalidCRC:       return "Invalid CRC";
    default:                                  return "Unknown error";
  }
}

// ── Constructor ──────────────────────────────────────────────────────────────

ModbusSHT20::ModbusSHT20(int rxPin, int txPin)
  : _rxPin(rxPin), _txPin(txPin), _lastError(ModbusMaster::ku8MBSuccess) {}

// ── begin() — start the serial port ──────────────────────────────────────────

bool ModbusSHT20::begin(long baudrate) {
  // Map Serial1 to our RS485 pins (ESP32 lets you assign any UART to any pins)
  Serial1.begin(baudrate, SERIAL_8N1, _rxPin, _txPin);
  return true;
}

// ── readSensor() — read temperature + humidity from input registers ──────────

bool ModbusSHT20::readSensor(int slaveId, float &temperature, float &humidity) {
  _node.begin(slaveId, Serial1);

  // Read 2 input registers starting at 0x0001 (temp=0x0001, humidity=0x0002)
  _lastError = _node.readInputRegisters(0x0001, 2);

  if (_lastError == ModbusMaster::ku8MBSuccess) {
    // Sensor returns values ×10 (e.g. 235 = 23.5°C), so divide by 10
    temperature = _node.getResponseBuffer(0) / 10.0;
    humidity    = _node.getResponseBuffer(1) / 10.0;
    return true;
  }

  return false;
}

// ── readConfig() — read all four config registers ────────────────────────────

bool ModbusSHT20::readConfig(int slaveId, Config &config) {
  uint16_t value;

  if (!readHoldingRegister(slaveId, REG_SLAVE_ID, value)) return false;
  config.slaveId = value;

  if (!readHoldingRegister(slaveId, REG_BAUD_RATE, value)) return false;
  config.baudRate = value;

  if (!readHoldingRegister(slaveId, REG_TEMPERATURE_COMPENSATION, value)) return false;
  config.temperatureCompensationTenthsC = toSignedRegister(value);

  if (!readHoldingRegister(slaveId, REG_HUMIDITY_COMPENSATION, value)) return false;
  config.humidityCompensationTenthsPercent = toSignedRegister(value);

  return true;
}

// ── writeSlaveId() ───────────────────────────────────────────────────────────

bool ModbusSHT20::writeSlaveId(int currentSlaveId, uint16_t newSlaveId) {
  if (newSlaveId < 1 || newSlaveId > 247) return false;
  return writeHoldingRegister(currentSlaveId, REG_SLAVE_ID, newSlaveId);
}

// ── writeBaudRate() ──────────────────────────────────────────────────────────

bool ModbusSHT20::writeBaudRate(int slaveId, uint16_t baudRate) {
  if (!isValidBaudRate(baudRate)) return false;
  return writeHoldingRegister(slaveId, REG_BAUD_RATE, baudRate);
}

// ── writeConfigBlock() — write all four config registers in one transaction ──

bool ModbusSHT20::writeConfigBlock(int currentSlaveId, const Config &config) {
  if (config.slaveId < 1 || config.slaveId > 247 || !isValidBaudRate(config.baudRate))
    return false;

  if (config.temperatureCompensationTenthsC < -100 || config.temperatureCompensationTenthsC > 100 ||
      config.humidityCompensationTenthsPercent < -100 || config.humidityCompensationTenthsPercent > 100)
    return false;

  _node.begin(currentSlaveId, Serial1);

  // Set the transmit buffer with the four register values
  _node.setTransmitBuffer(0, config.slaveId);
  _node.setTransmitBuffer(1, config.baudRate);
  _node.setTransmitBuffer(2, fromSignedRegister(config.temperatureCompensationTenthsC));
  _node.setTransmitBuffer(3, fromSignedRegister(config.humidityCompensationTenthsPercent));

  // Write 4 holding registers starting at REG_SLAVE_ID (0x0101)
  _lastError = _node.writeMultipleRegisters(REG_SLAVE_ID, 4);

  return _lastError == ModbusMaster::ku8MBSuccess;
}

// ── writeTemperatureCompensation() ───────────────────────────────────────────

bool ModbusSHT20::writeTemperatureCompensation(int slaveId, float compensationC) {
  if (compensationC < -10.0 || compensationC > 10.0) return false;
  return writeHoldingRegister(slaveId, REG_TEMPERATURE_COMPENSATION,
                              fromSignedRegister((int16_t)round(compensationC * 10.0)));
}

// ── writeHumidityCompensation() ──────────────────────────────────────────────

bool ModbusSHT20::writeHumidityCompensation(int slaveId, float compensationPercent) {
  if (compensationPercent < -10.0 || compensationPercent > 10.0) return false;
  return writeHoldingRegister(slaveId, REG_HUMIDITY_COMPENSATION,
                              fromSignedRegister((int16_t)round(compensationPercent * 10.0)));
}

// ── lastError() ──────────────────────────────────────────────────────────────

const char *ModbusSHT20::lastError() {
  return modbusErrorString(_lastError);
}

// ── Private helpers ──────────────────────────────────────────────────────────

bool ModbusSHT20::readHoldingRegister(int slaveId, uint16_t address, uint16_t &value) {
  _node.begin(slaveId, Serial1);
  _lastError = _node.readHoldingRegisters(address, 1);

  if (_lastError == ModbusMaster::ku8MBSuccess) {
    value = _node.getResponseBuffer(0);
    return true;
  }

  return false;
}

bool ModbusSHT20::writeHoldingRegister(int slaveId, uint16_t address, uint16_t value) {
  _node.begin(slaveId, Serial1);
  _lastError = _node.writeSingleRegister(address, value);

  return _lastError == ModbusMaster::ku8MBSuccess;
}

bool ModbusSHT20::isValidBaudRate(uint16_t baudRate) {
  return baudRate == 9600 || baudRate == 14400 || baudRate == 19200;
}

int16_t ModbusSHT20::toSignedRegister(uint16_t value) {
  return (int16_t)value;
}

uint16_t ModbusSHT20::fromSignedRegister(int16_t value) {
  return (uint16_t)value;
}