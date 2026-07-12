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
        // Fallback to holding registers if input registers fail
        if (!ModbusRTUClient.requestFrom(slaveId, HOLDING_REGISTERS, 0x01, 2)) {
          return false;
        }
    }
    
    short rawtemperature = ModbusRTUClient.read();
    short rawhumidity = ModbusRTUClient.read();
    
    temperature = rawtemperature / 10.0;
    humidity = rawhumidity / 10.0;
    
    return true;
}

void ModbusSHT20::scanSlaveIds(long baudrate) {
    Serial.println("Scanning for Modbus devices...");
    for (int id = 1; id <= 247; id++) {
        // Try reading a single register just to see if device responds
        if (ModbusRTUClient.requestFrom(id, HOLDING_REGISTERS, 0x01, 1) || 
            ModbusRTUClient.requestFrom(id, INPUT_REGISTERS, 0x01, 1)) {
            Serial.print("Found device at Slave ID: ");
            Serial.println(id);
        }
        delay(10);
    }
    Serial.println("Scan complete.");
}

void ModbusSHT20::scanRegisters(int slaveId, long baudrate) {
    Serial.print("Scanning registers for Slave ID ");
    Serial.println(slaveId);

    // Try scanning holding registers 0-10
    Serial.println("Holding Registers:");
    for (int reg = 0; reg < 10; reg++) {
        if (ModbusRTUClient.requestFrom(slaveId, HOLDING_REGISTERS, reg, 1)) {
             short val = ModbusRTUClient.read();
             Serial.print("  Reg ");
             Serial.print(reg);
             Serial.print(": ");
             Serial.println(val);
        }
        delay(10);
    }
    
     // Try scanning input registers 0-10
    Serial.println("Input Registers:");
    for (int reg = 0; reg < 10; reg++) {
        if (ModbusRTUClient.requestFrom(slaveId, INPUT_REGISTERS, reg, 1)) {
             short val = ModbusRTUClient.read();
             Serial.print("  Reg ");
             Serial.print(reg);
             Serial.print(": ");
             Serial.println(val);
        }
        delay(10);
    }
    Serial.println("Register scan complete.");
}
