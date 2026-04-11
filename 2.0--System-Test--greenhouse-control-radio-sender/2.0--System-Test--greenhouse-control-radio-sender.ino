#include <Arduino.h>
#include <stdarg.h>
#if defined(ARDUINO_ARCH_AVR)
#include <SoftwareSerial.h>
#endif
#include "GreenhouseControlNode.h"
#include "RYLR_LoRaAT_Software_Serial.h"

#if defined(ARDUINO_ARCH_ESP32)
#define LORA_RX RX
#define LORA_TX TX
#elif defined(ARDUINO_ARCH_AVR)
#define LORA_RX 2
#define LORA_TX 3
#endif

// Address of this sender and the forwarder we're sending to
#define LOCAL_ADDRESS 1
#define REMOTE_ADDRESS 2

RYLR_LoRaAT_Software_Serial rylr;
GreenhouseControlNode greenhouseNode;

#if defined(ARDUINO_ARCH_AVR)
SoftwareSerial radioSerial(LORA_RX, LORA_TX);
#endif

static void debugLogf(const char* format, ...) {
    char buffer[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
}

static Stream& loraSerial() {
#if defined(ARDUINO_ARCH_ESP32)
    return Serial1;
#else
    return radioSerial;
#endif
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    greenhouseNode.setupStatusLed();

#if defined(ARDUINO_ARCH_ESP32)
    Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);
#else
    radioSerial.begin(9600);
#endif
    rylr.setSerial(&loraSerial());

    int result = rylr.checkStatus();
    debugLogf("LoRa status: %d\n", result);

    rylr.setAddress(LOCAL_ADDRESS);
    rylr.setRFPower(14);
    greenhouseNode.begin(&rylr, REMOTE_ADDRESS);
    debugLogf("Greenhouse LoRa Sender node ready.\n");
}

void loop() {
    greenhouseNode.tick(millis());
    receiveReplies();
}

void receiveReplies() {
    RYLR_LoRaAT_Software_Serial_Message *message = rylr.checkMessage();
    if (message) {
        greenhouseNode.noteActivity(millis());
        debugLogf("RX from %d (RSSI %d, SNR %d) [%d bytes]: %s\n",
                  message->from_address, message->rssi,
                  message->snr, message->data_len, message->data);

        greenhouseNode.handleIncomingMessage(message->data);
    }
}
