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

void setup() {
    Serial.begin(115200);
    delay(2000);

    greenhouseNode.setupStatusLed();

    radioSerial.begin(9600);
    rylr.setSerial(&radioSerial);

    int result = rylr.checkStatus();
    Serial.print("LoRa status: ");
    Serial.println(result);

    rylr.setAddress(LOCAL_ADDRESS);
    rylr.setRFPower(14);
    greenhouseNode.begin(&rylr, REMOTE_ADDRESS);
    Serial.println("Greenhouse LoRa Sender node ready.");
}

void loop() {
    greenhouseNode.tick(millis());
    receiveReplies();
}

void receiveReplies() {
    RYLR_LoRaAT_Software_Serial_Message* message = rylr.checkMessage();
    if (message) {
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
    }
}
