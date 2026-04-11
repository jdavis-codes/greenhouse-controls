#include <Arduino.h>

#define LORA_RX RX
#define LORA_TX TX

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n--- RYLR LoRaAT Module Baud Rate Configuration ---");
    Serial.println("Attempting to connect at 115200 baud to configure to 9600...");

    // Connect to the LoRa module at default factory baud (usually 115200)
    Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
    
    Serial.println("Sending AT test command...");
    Serial1.println("AT");
    delay(500);
    while (Serial1.available()) {
        Serial.write(Serial1.read());
    }

    // Try to get current network parameters to verify connection
    Serial.println("\nTesting current network parameters...");
    Serial1.println("AT+NETWORKID?");
    delay(500);
    while (Serial1.available()) {
        Serial.write(Serial1.read());
    }

    // Send command to set Baud rate
    Serial.println("\nSetting module to 9600 baud...");
    // 9600 baud is the most stable parameter for ATmega328p SoftwareSerial
    Serial1.println("AT+IPR=9600");
    delay(500);
    
    // Read the response from the module (should be "+OK")
    while (Serial1.available()) {
        Serial.write(Serial1.read());
    }

    Serial.println("\nReconnecting at 9600 baud to verify...");
    // End the current Serial1 instance and restart it at 9600
    Serial1.end();
    delay(500);
    Serial1.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
    
    Serial.println("Sending AT test command at 9600 baud...");
    Serial1.println("AT");
    delay(500);
    
    // Verify we can still talk to it
    bool success = false;
    while (Serial1.available()) {
        char c = Serial1.read();
        Serial.write(c);
        if (c == 'K') success = true; // Primitive check for "+OK"
    }

    if (success) {
        Serial.println("\n✅ SUCCESS: Module is now permanently configured for 9600 baud!");
        Serial.println("You can now safely use it on the Arduino Nano with SoftwareSerial.");
    } else {
        Serial.println("\n❌ FAILED: Did not get expected response at 9600 baud.");
        Serial.println("If it was already 9600, power cycle it and run this tool but starting at 9600.");
    }
}

void loop() {
    // Act as a simple passthrough if you want to send manual AT commands
    if (Serial.available()) {
        Serial1.write(Serial.read());
    }
    if (Serial1.available()) {
        Serial.write(Serial1.read());
    }
}
