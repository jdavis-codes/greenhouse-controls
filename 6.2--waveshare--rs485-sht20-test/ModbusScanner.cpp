#include "ModbusScanner.h"

void ModbusScanner::scanSlaveIds(int startSlaveId, int endSlaveId) {
    Serial.printf("\n--- Starting Modbus Network Scan (Slave IDs %d-%d) ---\n", startSlaveId, endSlaveId);
    int foundCount = 0;
    
    for (int id = startSlaveId; id <= endSlaveId; id++) {
        // Try reading a single register just to see if device responds (Register 0 or 1)
        if (ModbusRTUClient.requestFrom(id, HOLDING_REGISTERS, 0x01, 1) || 
            ModbusRTUClient.requestFrom(id, INPUT_REGISTERS, 0x01, 1) ||
            ModbusRTUClient.requestFrom(id, HOLDING_REGISTERS, 0x00, 1)) {
            
            Serial.printf("-> Found device responding at Slave ID: %d\n", id);
            foundCount++;
            
            // Note: Clear out the read buffer since we just probed it mapping
            while (ModbusRTUClient.available()) {
                ModbusRTUClient.read();
            }
        } else {
                Serial.printf("-> No response from Slave ID: %d\n", id);
        }
        delay(10);
    }
    Serial.printf("--- Scan Complete. Found %d devices. ---\n\n", foundCount);
}

void ModbusScanner::scanRegisters(int slaveId, int startReg, int endReg, bool scanCoils, bool scanDiscreteInputs, bool scanHoldingRegisters, bool scanInputRegisters) {
    Serial.printf("\n--- Scanning Registers for Slave ID %d (Reg %d to %d) ---\n", slaveId, startReg, endReg);

    // Scan Coils
    if (scanCoils){
        Serial.println("Coils (0x01):");
        int coilsFound = 0;
        for (int reg = startReg; reg <= endReg; reg++) {
            if (ModbusRTUClient.requestFrom(slaveId, COILS, reg, 1)) {
                short val = ModbusRTUClient.read();
                Serial.printf("  [Coil 0x%04X] Val: %d\n", reg, val);
                coilsFound++;
            }
            delay(10);
        }
        if (coilsFound == 0) Serial.println("  (None responded)");
    }

    // Scan Discrete Inputs
    if (scanDiscreteInputs){
        Serial.println("Discrete Inputs (0x02):");
        int discreteFound = 0;
        for (int reg = startReg; reg <= endReg; reg++) {
            if (ModbusRTUClient.requestFrom(slaveId, DISCRETE_INPUTS, reg, 1)) {
                short val = ModbusRTUClient.read();
                Serial.printf("  [Discrete 0x%04X] Val: %d\n", reg, val);
                discreteFound++;
            }
            delay(10);
        }
        if (discreteFound == 0) Serial.println("  (None responded)");
    }

    // Scan Holding Registers
    if (scanHoldingRegisters){
        Serial.println("Holding Registers (0x03):");
        int holdingFound = 0;
        for (int reg = startReg; reg <= endReg; reg++) {
            if (ModbusRTUClient.requestFrom(slaveId, HOLDING_REGISTERS, reg, 1)) {
                short val = ModbusRTUClient.read();
                Serial.printf("  [Reg 0x%04X] Val: %d (0x%04X)\n", reg, val, (uint16_t)val);
                holdingFound++;
            }
            delay(10);
        }
        if (holdingFound == 0) Serial.println("  (None responded)");
    }

    // Scan Input Registers
    if (scanInputRegisters){
        Serial.println("Input Registers (0x04):");
        int inputFound = 0;
        for (int reg = startReg; reg <= endReg; reg++) {
            if (ModbusRTUClient.requestFrom(slaveId, INPUT_REGISTERS, reg, 1)) {
                short val = ModbusRTUClient.read();
                Serial.printf("  [Reg 0x%04X] Val: %d (0x%04X)\n", reg, val, (uint16_t)val);
                inputFound++;
            }
            delay(10);
        }
        if (inputFound == 0) Serial.println("  (None responded)");
        
        Serial.println("--- Register Scan Complete ---\n");
    }
}

void ModbusScanner::scanAll(int startSlaveId, int endSlaveId, int startReg, int endReg, unsigned long timeoutMs,  bool scanCoils, bool scanDiscreteInputs, bool scanHoldingRegisters, bool scanInputRegisters) {
    // Store original timeout to restore later
    // ArduinoModbus default is 1000ms, which is very slow for scanning 247 IDs.
    ModbusRTUClient.setTimeout(timeoutMs);

    Serial.printf("\n=== Full Network & Register Scan (IDs %d-%d) ===\n", startSlaveId, endSlaveId);
    for (int id = startSlaveId; id <= endSlaveId; id++) {
        // Quick probe
        if (ModbusRTUClient.requestFrom(id, HOLDING_REGISTERS, 0x01, 1) || 
            ModbusRTUClient.requestFrom(id, INPUT_REGISTERS, 0x01, 1) ||
            ModbusRTUClient.requestFrom(id, HOLDING_REGISTERS, 0x00, 1)) {
            
            Serial.printf("-> Active Device Found at Slave ID: %d\n", id);
            
            // Clear buffer
            while (ModbusRTUClient.available()) {
                ModbusRTUClient.read();
            }
            
            // Scan its registers
            scanRegisters(id, startReg, endReg, scanCoils, scanDiscreteInputs, scanHoldingRegisters, scanInputRegisters);
        } else {
                Serial.printf("-> No response from Slave ID: %d\n", id);
        }
        delay(10);
    }
    Serial.println("=== Full Scan Complete ===");
    ModbusRTUClient.setTimeout(1000); // restore default
}