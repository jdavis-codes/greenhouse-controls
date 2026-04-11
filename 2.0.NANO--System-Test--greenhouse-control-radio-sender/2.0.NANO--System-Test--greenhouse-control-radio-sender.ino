#include <Arduino.h>
#include <SoftwareSerial.h>
#include "GreenhouseControlNode.h"
#include "RYLR_LoRaAT_Software_Serial.h"

#define LORA_RX 2
#define LORA_TX 3

// Address of this sender and the forwarder we're sending to
#define LOCAL_ADDRESS 1
#define REMOTE_ADDRESS 2

RYLR_LoRaAT_Software_Serial rylr;
GreenhouseControlNode greenhouseNode;
SoftwareSerial radioSerial(LORA_RX, LORA_TX);

constexpr telegram_data_pipe data_pipe = radio;

int greenhouseTemp = 82;
int greenhouseHumidity = 58;
int soilMoisture = 45;
int insolation = 73;
bool fanOn = false;
bool sidesUp = false;
bool irrigationOn = false;

float targetTemp1 = 80.0f;
float targetTemp2 = 85.0f;
float tempDelta = 4.0f;
float targetMoisture = 50.0f;
float moistureDelta = 10.0f;

void sampleDemoData(unsigned long now) {
    greenhouseTemp = 78 + (now / 10000) % 15;
    greenhouseHumidity = 50 + (now / 8000) % 20;
    soilMoisture = 30 + (now / 12000) % 40;
    insolation = 20 + (now / 6000) % 60;
}

void onFanSet(bool state) {
    Serial.print("[CTRL] FAN -> ");
    Serial.println(state ? "ON" : "OFF");
}

void onSidesSet(bool state) {
    Serial.print("[CTRL] SIDES -> ");
    Serial.println(state ? "UP" : "DOWN");
}

void onWaterSet(bool state) {
    Serial.print("[CTRL] WATER -> ");
    Serial.println(state ? "ON" : "OFF");
}

GreenhouseControlNode::SensorBinding sensors[] = {
    {"GH_TEMP", &greenhouseTemp},
    {"GH_HUM", &greenhouseHumidity},
    {"SOIL", &soilMoisture},
    {"SUN", &insolation}
};

GreenhouseControlNode::EventBinding events[] = {
    {"FAN", &fanOn, onFanSet},
    {"SIDES", &sidesUp, onSidesSet},
    {"WATER", &irrigationOn, onWaterSet}
};

GreenhouseControlNode::SettingBinding settings[] = {
    {"TEMP1", &targetTemp1},
    {"TEMP2", &targetTemp2},
    {"TDELTA", &tempDelta},
    {"MOIST", &targetMoisture},
    {"MDELTA", &moistureDelta}
};

void setup() {
    Serial.begin(115200);
    delay(2000);

    greenhouseNode.setupStatusLed();
    greenhouseNode.configure(sensors, events, settings, sampleDemoData);

    switch (data_pipe) {
        case radio: {
            radioSerial.begin(9600);
            rylr.setSerial(&radioSerial);

            if (rylr.checkStatus() != 0) {
                Serial.println("LoRa not at 9600. Trying 115200...");
                radioSerial.begin(115200);
                if (rylr.checkStatus() == 0) {
                    Serial.println("LoRa found at 115200. Setting to 9600...");
                    radioSerial.print("AT+IPR=9600\r\n");
                    delay(500);
                    
                    radioSerial.begin(9600);
                    if (rylr.checkStatus() == 0) {
                        Serial.println("Successfully switched to 9600.");
                    } else {
                        Serial.println("Warning: 9600 verification failed.");
                    }
                } else {
                    Serial.println("Radio not responding. Falling back to 9600.");
                    radioSerial.begin(9600);
                }
            } else {
                Serial.println("LoRa already at 9600.");
            }

            rylr.setAddress(LOCAL_ADDRESS);
            rylr.setRFPower(14);
            greenhouseNode.begin(&rylr, REMOTE_ADDRESS, data_pipe);
            Serial.println("Greenhouse sender ready - transmitting via the rylr RADIO data pipe.");
            break;
        }
        case uart_rx_tx: {
            radioSerial.begin(9600);
            greenhouseNode.begin(nullptr, REMOTE_ADDRESS, data_pipe, &radioSerial);
            Serial.println("Greenhouse sender ready - transmitting via the UART RX/TX data pipe.");
            break;
        }
    }
}

void loop() {
    greenhouseNode.tick(millis());
    receiveReplies();
}

void receiveReplies() {
    switch (data_pipe) {
        case radio: {
            RYLR_LoRaAT_Software_Serial_Message* message = rylr.checkMessage();
            if (!message) return;

            greenhouseNode.noteActivity(millis());
            Serial.print("RX from ");
            Serial.print(message->from_address);
            Serial.print(" (RSSI ");
            Serial.print(message->rssi);
            Serial.print(", SNR ");
            Serial.print(message->snr);
            Serial.print(") [");
            Serial.print(message->data_len);
            Serial.print(" bytes]: ");
            Serial.println(message->data);

            greenhouseNode.handleIncomingMessage(message->data);
            break;
        }
        case uart_rx_tx: {
            char line[128];
            if (!greenhouseNode.readLineFromSerial(line, sizeof(line))) return;

            greenhouseNode.noteActivity(millis());
            Serial.print("RX UART: ");
            Serial.println(line);
            greenhouseNode.handleIncomingMessage(line);
            break;
        }
    }
}
