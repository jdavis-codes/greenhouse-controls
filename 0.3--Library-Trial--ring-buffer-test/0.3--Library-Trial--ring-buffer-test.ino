#include <Arduino.h>
#include <RTClib.h>
#include "freertos/ringbuf.h"
#include "esp_heap_caps.h"

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

// Global buffer pointer (allocated dynamically in setup)
RingBuffer* history = nullptr;

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for Serial monitor to attach

    Serial.println("\n--- Starting Ring Buffer Tester (Option A) ---");
    Serial.printf("LogEntry struct size: %u bytes\n", sizeof(LogEntry));

    if (psramFound()) {
        Serial.println("PSRAM detected on board!");
        Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
        Serial.printf("Free PSRAM:  %u bytes\n", ESP.getFreePsram());
    } else {
        Serial.println("WARNING: PSRAM not found. Make sure your board actually has PSRAM and build flags are set correctly.");
    }

    // Try allocating a huge buffer for history based on available contiguous memory
    size_t TARGET_CAPACITY = 100000;
    if (psramFound()) {
        // Find max contiguous block and use 95% of it for safety
        size_t max_block = ESP.getMaxAllocPsram();
        TARGET_CAPACITY = (max_block * 0.95) / sizeof(LogEntry);
        Serial.printf("Max contiguous PSRAM block: %u bytes\n", max_block);
    }
    
    Serial.printf("Attempting to allocate buffer for %u entries (Max test)...\n", TARGET_CAPACITY);
    history = new RingBuffer(TARGET_CAPACITY);

    if (history->isInitialized()) {
        Serial.println("Allocation SUCCESS!");
        Serial.printf("Allocated Size: %u bytes\n", history->getByteSize());
        Serial.print("Memory Location: ");
        Serial.println(history->isUsingPSRAM() ? "PSRAM" : "Internal RAM");
    } else {
        Serial.println("Allocation FAILED. Try smaller size.");
        return;
    }

    // Fill the ENTIRE buffer with mock data
    Serial.printf("Writing %u mock items...\n", TARGET_CAPACITY);
    uint32_t start_time = millis();
    
    for (size_t i = 0; i < TARGET_CAPACITY; i++) {
        LogEntry mock;
        mock.timestamp = DateTime(2026, 4, 6, 12, 0, 0) + TimeSpan(i * 300); // 5 min intervals
        mock.grnhouseTemp = 70.0f + (i % 20);
        mock.grnhouseHum = 50.0f;
        mock.ambientTemp = 60.0f;
        mock.ambientHum = 40.0f;
        mock.insolation = 80.0f;
        mock.soilMoisture = 50.0f;
        mock.fanOn = (i % 2 == 0);
        mock.motorUp = false;
        mock.waterOn = false;
        
        history->push_back(mock);
    }
    
    uint32_t write_time = millis() - start_time;
    Serial.printf("Wrote %u items in %u ms\n", TARGET_CAPACITY, write_time);

    // Read all items to benchmark reading
    Serial.printf("Reading %u mock items from custom buffer...\n", TARGET_CAPACITY);
    uint32_t read_start = millis();
    size_t custom_read_count = 0;
    // Volatile to prevent compiler from optimizing out the read loop
    volatile float dummySum = 0;
    for (size_t i = 0; i < history->getCount(); i++) {
        LogEntry e = history->get(i);
        dummySum += e.grnhouseTemp; // Actually use the data so it isn't optimized
        custom_read_count++;
    }
    uint32_t read_time = millis() - read_start;
    Serial.printf("Read %u items in %u ms\n", custom_read_count, read_time);

    // Read back a few latest records to verify
    Serial.println("Reading back last 3 entries:");
    for (size_t i = history->getCount() - 3; i < history->getCount(); i++) {
        LogEntry e = history->get(i);
        char buf[] = "YYYY-MM-DD hh:mm:ss"; // RTClib expects the format string pre-loaded
        e.timestamp.toString(buf);
        Serial.printf("  [%u] Time: %s, Temp: %.1f\n", i, buf, e.grnhouseTemp);
    }

    Serial.println("\n--- FreeRTOS RingBuffer Benchmark ---");
    Serial.printf("Free PSRAM before: %u bytes\n", ESP.getFreePsram());
    // FreeRTOS ring buffers require an extra 8 bytes overhead per item in NOSPLIT mode.
    // Plus alignment rounding.
    size_t item_size = (sizeof(LogEntry) + 3) & ~3; // Align to 32 bits
    size_t rtos_capacity = 50000;
    size_t rtos_buffer_size = rtos_capacity * (item_size + 8); // 8 byte header per item

    StaticRingbuffer_t* rtos_struct = NULL;
    uint8_t* rtos_storage = NULL;
    RingbufHandle_t rtos_handle = NULL;

    Serial.printf("Attempting to allocate FreeRTOS RingBuffer for %u entries (%u bytes)...\n", rtos_capacity, rtos_buffer_size);
    rtos_struct = (StaticRingbuffer_t*)heap_caps_malloc(sizeof(StaticRingbuffer_t), MALLOC_CAP_SPIRAM);
    rtos_storage = (uint8_t*)heap_caps_malloc(rtos_buffer_size, MALLOC_CAP_SPIRAM);

    if (rtos_struct && rtos_storage) {
        rtos_handle = xRingbufferCreateStatic(rtos_buffer_size, RINGBUF_TYPE_NOSPLIT, rtos_storage, rtos_struct);
        if (rtos_handle) {
            Serial.println("FreeRTOS RingBuffer allocation SUCCESS!");
            Serial.printf("Free PSRAM after alloc: %u bytes\n", ESP.getFreePsram());
            Serial.println("Writing 50,000 mock items...");
            uint32_t rtos_start_time = millis();

            for (int i = 0; i < 50000; i++) {
                LogEntry mock;
                mock.timestamp = DateTime(2026, 4, 6, 12, 0, 0) + TimeSpan(i * 300); // 5 min intervals
                mock.grnhouseTemp = 70.0f + (i % 20);
                mock.grnhouseHum = 50.0f;
                mock.ambientTemp = 60.0f;
                mock.ambientHum = 40.0f;
                mock.insolation = 80.0f;
                mock.soilMoisture = 50.0f;
                mock.fanOn = (i % 2 == 0);
                mock.motorUp = false;
                mock.waterOn = false;

                UBaseType_t res = xRingbufferSend(rtos_handle, &mock, sizeof(LogEntry), 0);
            }

            uint32_t rtos_write_time = millis() - rtos_start_time;
            Serial.printf("Wrote 50,000 FreeRTOS items in %u ms\n", rtos_write_time);

            // Time to read them back (reading just drains it)
            uint32_t rtos_read_start = millis();
            size_t rtos_read_count = 0;
            size_t ret_item_size;
            LogEntry* item;
            while ((item = (LogEntry*)xRingbufferReceive(rtos_handle, &ret_item_size, 0)) != NULL) {
                rtos_read_count++;
                vRingbufferReturnItem(rtos_handle, (void*)item);
            }
            uint32_t rtos_read_time = millis() - rtos_read_start;
            Serial.printf("Read %u FreeRTOS items in %u ms\n", rtos_read_count, rtos_read_time);

            vRingbufferDelete(rtos_handle);
        } else {
            Serial.println("xRingbufferCreateStatic returned NULL");
        }
    } else {
        Serial.println("Failed to allocate PSRAM for FreeRTOS RingBuffer");
    }

    if (rtos_struct) free(rtos_struct);
    if (rtos_storage) free(rtos_storage);

}

void loop() {
    // Nothing more to do
    delay(1000);
}
