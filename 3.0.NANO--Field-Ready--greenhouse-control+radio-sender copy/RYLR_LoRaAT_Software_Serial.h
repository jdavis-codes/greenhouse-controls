// Copyright (c) James Wanderer. All rights reserved
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

//
// Simple driver for Reyax LoRA devices with a UART Stream interface.
//

#ifndef RYLR_LORAAT_SOFTWARE_SERIAL_H
#define RYLR_LORAAT_SOFTWARE_SERIAL_H

#include <Arduino.h>

#define READ_TIMEOUT_MS 100                 // 100 ms
#define SEND_TIMEOUT_MS 1000                // 1 second
#define MAX_DATA_LEN 64                     // Reduced from 100 to 64 bytes
#define MAX_TX_MSG (MAX_DATA_LEN + 20)
#define MAX_RX_MSG (MAX_DATA_LEN + 28)
#define RX_BUFFER_LEN (MAX_RX_MSG * 2)      // Reduced multiplier from 3 to 2

// Received message
// Points to data in the RX Buffer
// Valid until next operation.
struct RYLR_LoRaAT_Software_Serial_Message {
    uint8_t from_address;
    uint16_t data_len;
    char* data;
    int rssi;
    int snr;
};

class RYLR_LoRaAT_Software_Serial {
public:
    RYLR_LoRaAT_Software_Serial();

    // Connect to any Arduino Stream (HardwareSerial or SoftwareSerial).
    void setSerial(Stream* serial) { this->serial = serial; }

    // Communicate with device. A return value of 0 means OK.
    int checkStatus();

    // Device address: 0 to 65535.
    int setAddress(uint16_t address);

    // RF Parameters.
    int setRFParameters(uint8_t spread, uint8_t bandwidth, uint8_t coding_rate, uint8_t preamble);

    // RF power: 0 to 22 (14 to comply with CE cert).
    int setRFPower(uint8_t power);

    // Set password. Must be 00000001 - FFFFFFFF.
    int setPassword(const char* password);

    // Receive a message.
    RYLR_LoRaAT_Software_Serial_Message* checkMessage();

    // Send a message.
    void startTxMessage();
    void addTxData(int len, const char* data);
    void addTxData(const char* data);
    void addTxData(int data);
    void addTxData(double data);
    int sendTxMessage(uint8_t to_address);

    // Debug functions.
    void dumpMessage();
    void dumpStats();

private:
    Stream* serial;

    // Read buffer and pointers.
    char rx_buffer[RX_BUFFER_LEN];
    int rx_index;
    int read_index;

    // RX message descriptor.
    RYLR_LoRaAT_Software_Serial_Message rx_message;

    // TX Buffer and pointers.
    char tx_buffer[MAX_TX_MSG];
    int tx_index;

    const char* RECV = "+RCV=";

    // Counters.
    int overflow_count;
    int overwrite_count;
    int rx_error_count;
    int tx_message_count;
    int rx_message_count;

    const char* getResponse();
    int processInput();
    int resultValue(const char* valstr);
    int parseDataLength(int data_size);
    void resetRxBuffer();
};

#endif