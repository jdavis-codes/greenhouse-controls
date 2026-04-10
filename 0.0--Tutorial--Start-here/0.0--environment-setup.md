# Step 1: Environment Setup

This guide will walk you through setting up your Arduino IDE to work with the sketches in this repository. While this project supports PlatformIO for advanced environments, Arduino IDE remains an excellent, fully-supported way to compile and load individual tools.

## Arduino IDE Setup

1. **Install Arduino IDE:** Download the latest version of Arduino IDE from the [Arduino website](https://www.arduino.cc/en/software).

2. **Add Boards:** go to the sidebar, and select board manager. install "Arduino AVR boards" and "Arduino ESP32 Boards"

3. **Select Your Sketch:** Go to **File > Open**, navigate to this repository's folder, and open the `.ino` file in the sketch folder you want to use (e.g., `arduino-greenhouse-controller/arduino-greenhouse-controller.ino`).

4. **Install Required Libraries:** All third-party libraries used in this project can be installed using the **Library Manager** in the Arduino IDE (**Sketch > Include Library > Manage Libraries**).

---

## Required Libraries Breakdown

Our sketches rely on several highly-capable open source libraries. Below is the detailed information and rationale for each library used across this project:

### 1. DHTStable
- **Author:** Rob Tillaart
- **GitHub:** [https://github.com/RobTillaart/DHTstable](https://github.com/RobTillaart/DHTstable)
- **Description:** A stable library for DHT11 and DHT22 temperature and humidity sensors.
- **Why we selected it:** We use multiple DHT sensors (one greenhouse, one ambient). This library provides excellent, non-blocking synchronous reads for AVR and ESP32 architectures without risking behavior anomalies in multi-sensor setups.

### 2. LiquidCrystal I2C
- **Author:** Frank de Brabander (maintained by Marco Schwartz / johnrickman)
- **GitHub:** [https://github.com/johnrickman/LiquidCrystal_I2C](https://github.com/johnrickman/LiquidCrystal_I2C)
- **Description:** A library for controlling I2C LCD displays.
- **Why we selected it:** Exposes an interface almost identical to the standard `LiquidCrystal` library. It seamlessly drives our 20x4 I2C display in the main `arduino-greenhouse-controller` and `resident1` experiments.

### 3. RTClib
- **Author:** Adafruit
- **GitHub:** [https://github.com/adafruit/RTClib](https://github.com/adafruit/RTClib)
- **Description:** A robust, cross-platform library for real-time clocks (DS1307, DS3231, PCF8523, etc.).
- **Dependencies:** Requires `Adafruit BusIO` (Arduino IDE will usually offer to install this automatically).
- **Why we selected it:** This provides our standard `DateTime` structures and timekeeping across the project. Our arrays and ring buffers heavily rely on RTClib's `DateTime` objects to accurately timestamp sensor datasets throughout telemetry transit.

### 4. FastBot2
- **Author:** AlexGyver
- **GitHub:** [https://github.com/GyverLibs/FastBot2](https://github.com/GyverLibs/FastBot2)
- **Description:** A fast, highly-optimized, next-generation Telegram Bot library for ESP8266 and ESP32.
- **Dependencies:** Requires `GSON` and `GyverHTTP`.
- **Why we selected it:** Used heavily in our advanced active dashboard scenarios (`lora-telegram-forwarder-greenhouse`). It's significantly faster than standard ArduinoJSON Telegram approaches, parses server responses very quickly, consumes very little SRAM, and supports interactive inline bot keyboards.

### 5. TelegramSerial
- **Author:** toastmanAu
- **GitHub:** [https://github.com/toastmanAu/TelegramSerial](https://github.com/toastmanAu/TelegramSerial)
- **Description:** A drop-in `Serial` replacement inherited from `Print` that logs output directly to a Telegram bot via WiFi.
- **Why we selected it:** Used in `TelegramSerial-test` and the basic `lora-telegram-forwarder`. It respects basic rate limits and requires virtually zero boilerplate logic to proxy plain serial strings to a given Telegram chat. It is perfect when we simply want a "remote serial monitor" over Telegram.

### 6. RYLR_LoRaAT
- **Author:** James Wanderer
- **GitHub:** [https://github.com/jmwanderer/RYLR_LoRaAT](https://github.com/jmwanderer/RYLR_LoRaAT)
- **Description:** Simple UART command library for the Reyax RYLR998 and RYLR993 LoRa modules.
- **Why we selected it:** The Reyax modules are controlled entirely over a simple AT command serial link. This library abstracts the string parsing required to extract `address`, `data`, `RSSI`, and `SNR` from `+RCV` responses, packaging it cleanly into predictable C++ structs. Used in all LoRa-enabled sender and forwarder sketches.

---

Once your libraries are set up, check out the `secrets.h` templates in the respective sketch folders to securely place your WiFi and Telegram Bot credentials before flashing to the board!