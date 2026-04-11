#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <Arduino.h>
#include <RTClib.h>

// Option A: Canonical LogEntry that stores everything including timestamp
struct LogEntry {
    DateTime timestamp;
    float grnhouseTemp;
    float grnhouseHum;
    float ambientTemp;
    float ambientHum;
    float insolation;
    float soilMoisture;
    
    // Using bitfields or simple bools for actuator states
    bool fanOn;
    bool motorUp;
    bool waterOn;
};

class RingBuffer {
private:
    LogEntry* buffer;
    size_t capacity;
    size_t head;
    size_t count;
    bool usingPSRAM;

public:
    RingBuffer(size_t max_samples) {
        capacity = max_samples;
        head = 0;
        count = 0;
        usingPSRAM = false;

        // Attempt to allocate in PSRAM first
        if (psramFound()) {
            buffer = (LogEntry*)ps_malloc(capacity * sizeof(LogEntry));
            if (buffer) {
                usingPSRAM = true;
            }
        }

        // Fallback to internal RAM if PSRAM allocation fails or PSRAM is missing
        if (!buffer) {
            buffer = (LogEntry*)malloc(capacity * sizeof(LogEntry));
        }
    }

    ~RingBuffer() {
        if (buffer) {
            free(buffer);
        }
    }

    bool isInitialized() const { return buffer != nullptr; }
    bool isUsingPSRAM() const { return usingPSRAM; }
    size_t getCapacity() const { return capacity; }
    size_t getCount() const { return count; }
    size_t getByteSize() const { return capacity * sizeof(LogEntry); }

    // Appends a new entry to the buffer, overwriting the oldest if full
    void push_back(const LogEntry& entry) {
        if (!buffer) return;
        buffer[head] = entry;
        head = (head + 1) % capacity;
        if (count < capacity) count++;
    }

    // Retrieves an entry by age index. 0 = oldest available, count-1 = newest
    LogEntry get(size_t index) const {
        if (index >= count) return LogEntry{}; // or handle out-of-bounds mapping
        size_t tail = (head + capacity - count) % capacity;
        size_t actual_index = (tail + index) % capacity;
        return buffer[actual_index];
    }
};

#endif // LOG_BUFFER_H