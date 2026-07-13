#include "ModbusScanner.h"

// ── Shared ModbusMaster node (static so all scanner methods share it) ────────

static ModbusMaster node;

// ── probeDevice() — quick check if a slave ID responds ───────────────────────

bool ModbusScanner::probeDevice(int slaveId) {
  node.begin(slaveId, Serial1);
  uint8_t result = node.readHoldingRegisters(0x0001, 1);
  if (result == ModbusMaster::ku8MBSuccess) return true;

  // Also try input registers as a fallback
  result = node.readInputRegisters(0x0001, 1);
  return result == ModbusMaster::ku8MBSuccess;
}

// ── scanSlaveIds() ───────────────────────────────────────────────────────────

void ModbusScanner::scanSlaveIds(int startSlaveId, int endSlaveId) {
  Serial.printf("\n--- Starting Modbus Network Scan (Slave IDs %d-%d) ---\n",
                startSlaveId, endSlaveId);
  int foundCount = 0;

  for (int id = startSlaveId; id <= endSlaveId; id++) {
    if (probeDevice(id)) {
      Serial.printf("-> Found device responding at Slave ID: %d\n", id);
      foundCount++;
    } else {
      Serial.printf("-> No response from Slave ID: %d\n", id);
    }
    delay(10);
  }

  Serial.printf("--- Scan Complete. Found %d devices. ---\n\n", foundCount);
}

// ── scanRegisters() ──────────────────────────────────────────────────────────

void ModbusScanner::scanRegisters(int slaveId, int startReg, int endReg,
                                  bool scanCoils, bool scanDiscreteInputs,
                                  bool scanHoldingRegisters, bool scanInputRegisters) {
  Serial.printf("\n--- Scanning Registers for Slave ID %d (Reg %d to %d) ---\n",
                slaveId, startReg, endReg);

  // ── Coils (function 0x01) ──────────────────────────────────────────────
  if (scanCoils) {
    Serial.println("Coils (0x01):");
    int coilsFound = 0;
    for (int reg = startReg; reg <= endReg; reg++) {
      node.begin(slaveId, Serial1);
      if (node.readCoils(reg, 1) == ModbusMaster::ku8MBSuccess) {
        uint16_t val = node.getResponseBuffer(0);
        Serial.printf("  [Coil 0x%04X] Val: %d\n", reg, val);
        coilsFound++;
      }
      delay(10);
    }
    if (coilsFound == 0) Serial.println("  (None responded)");
  }

  // ── Discrete Inputs (function 0x02) ────────────────────────────────────
  if (scanDiscreteInputs) {
    Serial.println("Discrete Inputs (0x02):");
    int discreteFound = 0;
    for (int reg = startReg; reg <= endReg; reg++) {
      node.begin(slaveId, Serial1);
      if (node.readDiscreteInputs(reg, 1) == ModbusMaster::ku8MBSuccess) {
        uint16_t val = node.getResponseBuffer(0);
        Serial.printf("  [Discrete 0x%04X] Val: %d\n", reg, val);
        discreteFound++;
      }
      delay(10);
    }
    if (discreteFound == 0) Serial.println("  (None responded)");
  }

  // ── Holding Registers (function 0x03) ──────────────────────────────────
  if (scanHoldingRegisters) {
    Serial.println("Holding Registers (0x03):");
    int holdingFound = 0;
    for (int reg = startReg; reg <= endReg; reg++) {
      node.begin(slaveId, Serial1);
      if (node.readHoldingRegisters(reg, 1) == ModbusMaster::ku8MBSuccess) {
        uint16_t val = node.getResponseBuffer(0);
        Serial.printf("  [Reg 0x%04X] Val: %d (0x%04X)\n", reg, val, val);
        holdingFound++;
      }
      delay(10);
    }
    if (holdingFound == 0) Serial.println("  (None responded)");
  }

  // ── Input Registers (function 0x04) ────────────────────────────────────
  if (scanInputRegisters) {
    Serial.println("Input Registers (0x04):");
    int inputFound = 0;
    for (int reg = startReg; reg <= endReg; reg++) {
      node.begin(slaveId, Serial1);
      if (node.readInputRegisters(reg, 1) == ModbusMaster::ku8MBSuccess) {
        uint16_t val = node.getResponseBuffer(0);
        Serial.printf("  [Reg 0x%04X] Val: %d (0x%04X)\n", reg, val, val);
        inputFound++;
      }
      delay(10);
    }
    if (inputFound == 0) Serial.println("  (None responded)");
  }

  Serial.println("--- Register Scan Complete ---\n");
}

// ── scanAll() — full network + register scan ─────────────────────────────────

void ModbusScanner::scanAll(int startSlaveId, int endSlaveId,
                            int startReg, int endReg,
                            unsigned long timeoutMs,
                            bool scanCoils, bool scanDiscreteInputs,
                            bool scanHoldingRegisters, bool scanInputRegisters) {
  // Set a shorter timeout for faster scanning (default is often 1000ms)
  Serial1.setTimeout(timeoutMs);

  Serial.printf("\n=== Full Network & Register Scan (IDs %d-%d) ===\n",
                startSlaveId, endSlaveId);

  for (int id = startSlaveId; id <= endSlaveId; id++) {
    if (probeDevice(id)) {
      Serial.printf("-> Active Device Found at Slave ID: %d\n", id);
      scanRegisters(id, startReg, endReg,
                    scanCoils, scanDiscreteInputs,
                    scanHoldingRegisters, scanInputRegisters);
    } else {
      Serial.printf("-> No response from Slave ID: %d\n", id);
    }
    delay(10);
  }

  Serial.println("=== Full Scan Complete ===");

  // Restore default timeout
  Serial1.setTimeout(1000);
}