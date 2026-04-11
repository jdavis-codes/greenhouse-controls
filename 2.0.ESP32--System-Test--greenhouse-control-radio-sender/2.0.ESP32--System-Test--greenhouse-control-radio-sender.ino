#include <Arduino.h>
#include "GreenhouseControlNode.h"
#include "RYLR_LoRaAT.h"

#define LORA_RX RX
#define LORA_TX TX

// Address of this sender and the forwarder we're sending to
#define LOCAL_ADDRESS 1
#define REMOTE_ADDRESS 2

RYLR_LoRaAT rylr;
GreenhouseControlNode greenhouseNode;

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
            Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
            rylr.setSerial(&Serial1);

            int result = rylr.checkStatus();
            Serial.print("LoRa status: ");
            Serial.println(result);

            rylr.setAddress(LOCAL_ADDRESS);
            rylr.setRFPower(14);
            greenhouseNode.begin(&rylr, REMOTE_ADDRESS, data_pipe);
            Serial.println("Greenhouse sender ready on RADIO pipe.");
            break;
        }
        case uart_rx_tx: {
            Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
            greenhouseNode.begin(nullptr, REMOTE_ADDRESS, data_pipe, &Serial1);
            Serial.println("Greenhouse sender ready on UART RX/TX pipe.");
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
            RYLR_LoRaAT_Message* message = rylr.checkMessage();
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
