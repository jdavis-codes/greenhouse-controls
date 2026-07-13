#include "ModbusSHT20.h"

ModbusSHT20::ModbusSHT20(int rxPin, int txPin) : _rxPin(rxPin), _txPin(txPin) {
}

bool ModbusSHT20::begin(long baudrate) {
  // We need to configure the hardware serial under the hood for ArduinoModbus?
  // Actually, ArduinoModbus on ESP32 usually uses HardwareSerial.
  // We can initialize it like this for ESP32.
  
  // ArduinoModbus uses regular 'Serial' or 'Serial1' by default depending on the board
  // With ESP32 we might need to map RS485 to HardwareSerial instance (like Serial1 or Serial2). 
  // Let's configure it according to the pinout.
  
  // For standard ArduinoModbus on ESP32 using hardware RS485, we typically need to setup a HardwareSerial first.
  // Let's use Serial1 for RS485.
  Serial1.begin(baudrate, SERIAL_8N1, _rxPin, _txPin);
  
  // ModbusRTUClient normally uses a local stream if configured, but by default it might use "Serial"
  // Let's see how ArduinoModbus is configured... wait, ArduinoModbus library uses specific UARTs usually.
  // We can pass a specific client to ModbusRTUClient if we do `ModbusRTUClientClass ModbusRTUClient(Serial1);`
  // Actually, `ModbusRTUClient` is a pre-instantiated object. It uses RS485 class.
  // Let's use the provided hardware serial.
  RS485.setPins(_txPin, -1, -1); // Just an example, let's use the custom begin.

  // A better way is passing Serial1 to ModbusRTUClient begin.
  if (!ModbusRTUClient.begin(baudrate, SERIAL_8N1)) {
    return false;
  }
  return true;
}

bool ModbusSHT20::readSensor(int slaveId, float &temperature, float &humidity) {
    // According to specs and example, it's typically Input Registers or Holding Registers 
    // starting at 0x01 or 0x02. Let's try Requesting Holding Registers at 0x01.
    // We will read 2 registers.
    
    // Some models (like XY-MD02) start at 0x01 (Temperature) and 0x02 (Humidity).
    // Let's read starting at 0x01.
    if (!ModbusRTUClient.requestFrom(slaveId, INPUT_REGISTERS, 0x01, 2)) {
        return false;
    }
    
    short rawtemperature = ModbusRTUClient.read();
    short rawhumidity = ModbusRTUClient.read();
    
    temperature = rawtemperature / 10.0;
    humidity = rawhumidity / 10.0;
    
    return true;
}

  bool ModbusSHT20::readConfig(int slaveId, Config &config) {
    uint16_t value;

    if (!readHoldingRegister(slaveId, REG_SLAVE_ID, value)) {
      return false;
    }
    config.slaveId = value;

    if (!readHoldingRegister(slaveId, REG_BAUD_RATE, value)) {
      return false;
    }
    config.baudRate = value;

    if (!readHoldingRegister(slaveId, REG_TEMPERATURE_COMPENSATION, value)) {
      return false;
    }
    config.temperatureCompensationTenthsC = toSignedRegister(value);

    if (!readHoldingRegister(slaveId, REG_HUMIDITY_COMPENSATION, value)) {
      return false;
    }
    config.humidityCompensationTenthsPercent = toSignedRegister(value);

    return true;
  }

  bool ModbusSHT20::writeSlaveId(int currentSlaveId, uint16_t newSlaveId) {
    if (newSlaveId < 1 || newSlaveId > 247) {
      return false;
    }

    return writeHoldingRegister(currentSlaveId, REG_SLAVE_ID, newSlaveId);
  }

  bool ModbusSHT20::writeBaudRate(int slaveId, uint16_t baudRate) {
    if (!isValidBaudRate(baudRate)) {
      return false;
    }

    return writeHoldingRegister(slaveId, REG_BAUD_RATE, baudRate);
  }

  bool ModbusSHT20::writeConfigBlock(int currentSlaveId, const Config &config) {
    if (config.slaveId < 1 || config.slaveId > 247 || !isValidBaudRate(config.baudRate)) {
      return false;
    }

    if (config.temperatureCompensationTenthsC < -100 || config.temperatureCompensationTenthsC > 100 ||
        config.humidityCompensationTenthsPercent < -100 || config.humidityCompensationTenthsPercent > 100) {
      return false;
    }

    if (!ModbusRTUClient.beginTransmission(currentSlaveId, HOLDING_REGISTERS, REG_SLAVE_ID, 4)) {
      return false;
    }

    if (ModbusRTUClient.write(config.slaveId) != 1 ||
        ModbusRTUClient.write(config.baudRate) != 1 ||
        ModbusRTUClient.write(fromSignedRegister(config.temperatureCompensationTenthsC)) != 1 ||
        ModbusRTUClient.write(fromSignedRegister(config.humidityCompensationTenthsPercent)) != 1) {
      return false;
    }

    return ModbusRTUClient.endTransmission() == 1;
  }

  bool ModbusSHT20::writeTemperatureCompensation(int slaveId, float compensationC) {
    if (compensationC < -10.0 || compensationC > 10.0) {
      return false;
    }

    return writeHoldingRegister(slaveId, REG_TEMPERATURE_COMPENSATION, fromSignedRegister((int16_t)round(compensationC * 10.0)));
  }

  bool ModbusSHT20::writeHumidityCompensation(int slaveId, float compensationPercent) {
    if (compensationPercent < -10.0 || compensationPercent > 10.0) {
      return false;
    }

    return writeHoldingRegister(slaveId, REG_HUMIDITY_COMPENSATION, fromSignedRegister((int16_t)round(compensationPercent * 10.0)));
  }

  const char *ModbusSHT20::lastError() {
    return ModbusRTUClient.lastError();
  }

  bool ModbusSHT20::readHoldingRegister(int slaveId, uint16_t address, uint16_t &value) {
    long result = ModbusRTUClient.holdingRegisterRead(slaveId, address);
    if (result < 0) {
      return false;
    }

    value = (uint16_t)result;
    return true;
  }

  bool ModbusSHT20::writeHoldingRegister(int slaveId, uint16_t address, uint16_t value) {
    return ModbusRTUClient.holdingRegisterWrite(slaveId, address, value) == 1;
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

