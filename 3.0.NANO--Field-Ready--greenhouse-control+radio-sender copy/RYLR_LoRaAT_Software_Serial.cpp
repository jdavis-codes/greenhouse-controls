// Copyright (c) James Wanderer. All rights reserved
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

//
// Driver for Reyax LoRA devices with a UART Stream interface.
//

#include "RYLR_LoRaAT_Software_Serial.h"

namespace {
// Tiny string helpers — avoids linking printf on AVR.
char* bufAppend(char* p, const char* s) { while (*s) *p++ = *s++; *p = '\0'; return p; }
char* bufAppendInt(char* p, int val) { char tmp[8]; itoa(val, tmp, 10); return bufAppend(p, tmp); }
}

RYLR_LoRaAT_Software_Serial::RYLR_LoRaAT_Software_Serial()
    : serial(NULL),
      rx_index(0),
      read_index(0),
      tx_index(0),
      overflow_count(0),
      overwrite_count(0),
      rx_error_count(0),
      tx_message_count(0),
      rx_message_count(0) {
}

int RYLR_LoRaAT_Software_Serial::checkStatus() {
    if (!this->serial) return -1;
    const char* cmd = "AT\r\n";
    this->serial->write(cmd, strlen(cmd));
    return resultValue(getResponse());
}

int RYLR_LoRaAT_Software_Serial::setAddress(uint16_t address) {
    if (!this->serial) return -1;
    char* p = bufAppend(this->tx_buffer, "AT+ADDRESS=");
    p = bufAppendInt(p, address);
    p = bufAppend(p, "\r\n");
    this->serial->write(this->tx_buffer, p - this->tx_buffer);
    return resultValue(getResponse());
}

int RYLR_LoRaAT_Software_Serial::setRFParameters(uint8_t spread, uint8_t bandwidth, uint8_t coding_rate, uint8_t preamble) {
    if (!this->serial) return -1;
    char* p = bufAppend(this->tx_buffer, "AT+PARAMETER=");
    p = bufAppendInt(p, spread); *p++ = ',';
    p = bufAppendInt(p, bandwidth); *p++ = ',';
    p = bufAppendInt(p, coding_rate); *p++ = ',';
    p = bufAppendInt(p, preamble);
    p = bufAppend(p, "\r\n");
    this->serial->write(this->tx_buffer, p - this->tx_buffer);
    return resultValue(getResponse());
}

int RYLR_LoRaAT_Software_Serial::setRFPower(uint8_t power) {
    if (!this->serial) return -1;
    char* p = bufAppend(this->tx_buffer, "AT+CRFOP=");
    p = bufAppendInt(p, power);
    p = bufAppend(p, "\r\n");
    this->serial->write(this->tx_buffer, p - this->tx_buffer);
    return resultValue(getResponse());
}

int RYLR_LoRaAT_Software_Serial::setPassword(const char* password) {
    if (!this->serial) return -1;
    char* p = bufAppend(this->tx_buffer, "AT+CPIN=");
    p = bufAppend(p, password);
    p = bufAppend(p, "\r\n");
    this->serial->write(this->tx_buffer, p - this->tx_buffer);
    return resultValue(getResponse());
}

void RYLR_LoRaAT_Software_Serial::startTxMessage() {
    tx_index = 0;
    memset(this->tx_buffer, 0xff, sizeof(this->tx_buffer));
}

void RYLR_LoRaAT_Software_Serial::addTxData(const char* data) {
    int len = min(strlen(data), size_t(MAX_DATA_LEN - this->tx_index));
    strncpy(this->tx_buffer + this->tx_index, data, len);
    this->tx_index += len;
}

void RYLR_LoRaAT_Software_Serial::addTxData(int len, const char* data) {
    len = min(len, MAX_DATA_LEN - this->tx_index);
    memcpy(this->tx_buffer + this->tx_index, data, len);
    this->tx_index += len;
}

void RYLR_LoRaAT_Software_Serial::addTxData(int data) {
    int max_len = MAX_DATA_LEN - this->tx_index;
    char tmp[8];
    itoa(data, tmp, 10);
    int count = strlen(tmp);
    int copy = min(max_len, count);
    memcpy(this->tx_buffer + this->tx_index, tmp, copy);
    this->tx_buffer[this->tx_index + copy] = 0xff;
    this->tx_index += copy;
}

void RYLR_LoRaAT_Software_Serial::addTxData(double data) {
    // Manual float formatting — avoids linking float printf on AVR.
    int max_len = MAX_DATA_LEN - this->tx_index;
    if (max_len <= 0) return;
    char tmp[16];
    char* t = tmp;
    if (data < 0) { *t++ = '-'; data = -data; }
    long whole = (long)data;
    long frac = (long)((data - whole) * 10000 + 0.5);
    t = bufAppendInt(t, (int)whole);
    *t++ = '.';
    // Pad with leading zeros for 4 decimal places
    if (frac < 1000) *t++ = '0';
    if (frac < 100)  *t++ = '0';
    if (frac < 10)   *t++ = '0';
    t = bufAppendInt(t, (int)frac);
    int count = t - tmp;
    int copy = min(max_len, count);
    memcpy(this->tx_buffer + this->tx_index, tmp, copy);
    this->tx_buffer[this->tx_index + copy] = 0xff;
    this->tx_index += copy;
}

int RYLR_LoRaAT_Software_Serial::sendTxMessage(uint8_t to_address) {
    if (!this->serial) return -1;
    char addr_str[4];
    char len_str[6];
    this->tx_message_count++;

    itoa(to_address, addr_str, 10);
    itoa(this->tx_index, len_str, 10);

    // Header.
    this->serial->print("AT+SEND=");
    this->serial->print(addr_str);
    this->serial->print(",");
    this->serial->print(len_str);
    this->serial->print(",");

    // Data.
    this->serial->write(this->tx_buffer, this->tx_index);

    // Tail.
    this->serial->print("\r\n");

    this->tx_index = 0;

    // Wait for send to complete.
    unsigned long now = millis();
    while (!this->serial->available() && (millis() - now < SEND_TIMEOUT_MS)) {
    }

    return resultValue(getResponse());
}

// Receive a message.
// Return NULL if no message is ready for reading.
RYLR_LoRaAT_Software_Serial_Message* RYLR_LoRaAT_Software_Serial::checkMessage() {
    if (this->rx_index == this->read_index && this->serial && this->serial->available()) {
        this->resetRxBuffer();
        this->processInput();
    }
    if (this->rx_index > this->read_index) {
        // TODO: handle more than one message.
        char* ptr1;
        char* ptr2;

        // Parse from address field.
        ptr1 = this->rx_buffer + this->read_index + 5;
        ptr2 = strchr(ptr1, ',');
        if (ptr2 == NULL) {
            this->resetRxBuffer();
            this->rx_error_count++;
            return NULL;
        }
        *ptr2 = '\0';
        this->rx_message.from_address = atoi(ptr1);

        // Parse length field.
        ptr1 = ptr2 + 1;
        ptr2 = strchr(ptr1, ',');
        if (ptr2 == NULL) {
            this->resetRxBuffer();
            this->rx_error_count++;
            return NULL;
        }
        *ptr2 = '\0';
        int length = atoi(ptr1);
        this->rx_message.data_len = length;

        // Extract data field.
        ptr1 = ptr2 + 1;
        ptr2 = ptr1 + length;

        if ((ptr2 > (this->rx_buffer + this->rx_index)) || *ptr2 != ',') {
            this->resetRxBuffer();
            this->rx_error_count++;
            return NULL;
        }
        *ptr2 = '\0';
        this->rx_message.data = ptr1;

        // Extract RSSI.
        ptr1 = ptr2 + 1;
        ptr2 = strchr(ptr1, ',');
        if (ptr2 == NULL) {
            this->resetRxBuffer();
            this->rx_error_count++;
            return NULL;
        }
        *ptr2 = '\0';
        this->rx_message.rssi = atoi(ptr1);

        // Extract SNR.
        ptr1 = ptr2 + 1;
        this->rx_message.snr = atoi(ptr1);
        ptr2 = ptr1 + strlen(ptr1) + 1;
        this->read_index = (ptr2 - this->rx_buffer);

        if (this->read_index >= this->rx_index) {
            // Read all of the contents of the buffer.
            this->resetRxBuffer();
        }

        // Return message.
        this->rx_message_count++;
        return &(this->rx_message);
    }
    return NULL;
}

// Process expected input from the LoRa device.
// Save any received messages in the RX buffer.
const char* RYLR_LoRaAT_Software_Serial::getResponse() {
    bool done = false;
    char* response = NULL;

    // Read until we get a response.
    // Save any received messages.
    while (!done) {
        int index = processInput();
        // Handle timeout.
        if (index == -1) {
            done = true;
            continue;
        }

        // If this is not a RECV message, process it. Otherwise save it and keep reading.
        if (strncmp(this->rx_buffer + index, RECV, strlen(RECV))) {
            // Found a response - trim from RX buffer.
            this->rx_index = index;
            response = this->rx_buffer + index;
            done = true;
        }
    }
    return response;
}

// Read a single response or message from the LoRa device.
// Save in the RX buffer.
//
// Returns:
//   index in RX buffer of read data
//   -1 on error
int RYLR_LoRaAT_Software_Serial::processInput() {
    bool done = false;
    int read_count = 0;
    int to_read_count = 0;
    bool parse_data_length = false;
    int comma_count = 0;

    if (!this->serial) {
        this->resetRxBuffer();
        return -1;
    }

    // Ensure enough space in buffer.
    if (this->rx_index > (RX_BUFFER_LEN - MAX_RX_MSG)) {
        this->overflow_count++;
        this->resetRxBuffer();
    }

    // Read until we see a \n character terminate packet.
    while (!done) {
        // Spin waiting for available data.
        unsigned long now = millis();
        while (!this->serial->available()) {
            if (millis() - now > READ_TIMEOUT_MS) {
                // Timeout.
                this->rx_error_count++;
                this->resetRxBuffer();
                return -1;
            }
        }
        int data = this->serial->read();
        if (data == -1) {
            // Timeout.
            this->resetRxBuffer();
            this->rx_error_count++;
            return -1;
        }

        // Count fields separated by commas.
        if (data == ',') {
            comma_count += 1;
        }

        // Save data in buffer.
        if (read_count <= MAX_RX_MSG) {
            this->rx_buffer[this->rx_index + read_count] = (char)data;
        }
        read_count += 1;

        // Check if we are parsing a RECV message.
        if (read_count == 5) {
            if (memcmp(this->rx_buffer + this->rx_index, RECV, strlen(RECV)) == 0) {
                parse_data_length = true;
            }
        }

        // Handle data field length.
        if (parse_data_length && (comma_count == 2)) {
            to_read_count = this->parseDataLength(read_count);
            parse_data_length = false;
        }

        if (data == '\n' && to_read_count == 0) {
            // Null terminate to separate multiple packets in buffer.
            this->rx_buffer[this->rx_index + read_count] = '\0';
            done = true;
        }
        if (to_read_count > 0) {
            to_read_count -= 1;
        }
    }

    if (read_count > MAX_RX_MSG) {
        this->resetRxBuffer();
        return -1;
    }

    int msg_start = this->rx_index;
    // Save the null termination in the buffer.
    this->rx_index += read_count + 1;
    return msg_start;
}

// Clear all data in the receive buffer.
void RYLR_LoRaAT_Software_Serial::resetRxBuffer() {
    this->rx_index = 0;
    this->read_index = 0;
}

// Extract the length value from an +RECV message.
// Return the value of the length field.
int RYLR_LoRaAT_Software_Serial::parseDataLength(int data_size) {
    char* ptr1 = (char*)memchr(this->rx_buffer + this->rx_index, ',', data_size);
    if (ptr1 == NULL) {
        this->rx_error_count++;
        return 0;
    }

    int offset = ptr1 - (this->rx_buffer + this->rx_index);
    char* ptr2 = (char*)memchr(ptr1 + 1, ',', data_size - offset - 1);
    if (ptr2 == NULL) {
        this->rx_error_count++;
        return 0;
    }

    *ptr2 = '\0';
    int length = atoi(ptr1 + 1);
    *ptr2 = ',';

    return length;
}

// Parse a command response, return value.
// 0  : OK
// >0 : Error value returned by LoRa device
// -1 : data format error
int RYLR_LoRaAT_Software_Serial::resultValue(const char* valstr) {
    if (valstr == NULL) {
        return -1;
    }

    if (strcmp(valstr, "+OK")) {
        return 0;
    }

    if (strncmp(valstr, "+ERR=", 5)) {
        return atoi(valstr + 5);
    }
    return -1;
}

void RYLR_LoRaAT_Software_Serial::dumpMessage() {
    if (this->tx_buffer[this->tx_index] != 0xff) {
        this->overwrite_count++;
        Serial.print(" - overwrite -");
    }

    this->tx_buffer[this->tx_index] = '\0';
    Serial.print("len = ");
    Serial.println(strlen(this->tx_buffer));
    Serial.print(this->tx_buffer);
    Serial.println();
}

void RYLR_LoRaAT_Software_Serial::dumpStats() {
    Serial.print("Overflow count: ");
    Serial.println(this->overflow_count);
    Serial.print("Buffer overwrite count: ");
    Serial.println(this->overwrite_count);
    Serial.print("RX Errors: ");
    Serial.println(this->rx_error_count);
    Serial.print("TX Messages: ");
    Serial.println(this->tx_message_count);
    Serial.print("RX Messages: ");
    Serial.println(this->rx_message_count);
    Serial.print("TX Index: ");
    Serial.println(this->tx_index);
    Serial.print("RX Index: ");
    Serial.println(this->rx_index);
    Serial.print("Read Index: ");
    Serial.println(this->read_index);
}