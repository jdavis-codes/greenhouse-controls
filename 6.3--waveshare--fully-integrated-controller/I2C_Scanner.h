#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include <Arduino.h>
#include <Wire.h>

// Simple I2C bus scanner — prints every device address that responds.
// Call once during setup() to verify your sensors and peripherals are wired.
class I2C_Scanner {
public:
  static void scan() {
    Serial.println("\n=== I2C Bus Scan ===");
    int found = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("  Device found at 0x%02X (%d)\n", addr, addr);
        found++;
      }
      delay(1);  // Give the bus a moment between probes
    }

    if (found == 0) {
      Serial.println("  No I2C devices found!");
    } else {
      Serial.printf("  %d device(s) found.\n", found);
    }
    Serial.println("========================\n");
  }
};

#endif