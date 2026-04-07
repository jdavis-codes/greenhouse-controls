# greenhouse-controls

This repository contains a greenhouse controller sketch plus several ESP32 experiments for Telegram and LoRa workflows. The project is set up so PlatformIO can build multiple independent sketches while each sketch folder stays usable in the Arduino IDE.

## What Is In This Repo

- `arduino-greenhouse-controller/`: the original greenhouse controller sketch.
- `TelegramSerial-test/`: ESP32 test sketch for the `TelegramSerial` library.
- `FastBot2-telegram-test/`: ESP32 test sketch for the `FastBot2` library.
- `telegram-forwarder/`: ESP32 sketch that receives LoRa packets and forwards them to Telegram.
- `lora-sender/`: ESP32 sketch that sends dummy structured greenhouse data over a Reyax RYLR radio.
- `lora-sender-greenhouse/`: Advanced sender that pairs with the full dashboard and executes remote commands over LoRa.
- `lora-telegram-forwarder-greenhouse/`: Advanced forwarder acting as a man-in-the-middle gateway. It catches LoRa telemetry, logs it to PSRAM, and serves the full interactive Telegram Dashboard.
- `set_src_dir.py`: shared PlatformIO pre-script that switches environments to the correct sketch folder.
- `platformio.ini`: environment definitions for each sketch.

## Design Goals

- Keep the main greenhouse sketch easy to open in the Arduino IDE.
- Let PlatformIO build and flash multiple independent experiments from one repo.
- Keep secrets out of Git.
- Make it easy to add a new test sketch without restructuring the repo.

## Project Layout

Each experiment lives in its own top-level folder with one `.ino` entrypoint. PlatformIO selects the active folder per environment using:

```ini
extra_scripts = pre:set_src_dir.py
custom_src_dir = your-sketch-folder
```

This means you do not need a shared `src/` directory for experiments, and Arduino IDE users can still open a sketch folder directly.

## PlatformIO Environments

Current environments in `platformio.ini`:

- `uno`: AVR build of the original greenhouse sketch.
- `esp32`: Arduino Nano ESP32 build of the original greenhouse sketch.
- `TelegramSerial_test`: TelegramSerial proof-of-concept sketch.
- `FastBot2_telegram_test`: FastBot2 proof-of-concept sketch.
- `telegram_forwarder`: receives LoRa data and forwards it to Telegram.
- `lora_sender`: sends dummy greenhouse payloads over LoRa.

Build an environment:

```bash
~/.platformio/penv/bin/pio run -e telegram_forwarder
```

Upload an environment:

```bash
~/.platformio/penv/bin/pio run -t upload -e lora_sender --upload-port /dev/cu.usbmodemXXXX
```

Clean an environment:

```bash
~/.platformio/penv/bin/pio run -t clean -e telegram_forwarder
```

## Arduino IDE Usage

If you are not using PlatformIO, open one of the sketch folders directly in the Arduino IDE:

- `arduino-greenhouse-controller/arduino-greenhouse-controller.ino`
- `TelegramSerial-test/TelegramSerial-test.ino`
- `FastBot2-telegram-test/FastBot2-telegram-test.ino`
- `telegram-forwarder/telegram-forwarder.ino`
- `lora-sender/lora-sender.ino`

Install the required libraries in Arduino IDE as needed. PlatformIO users get them through `lib_deps` in `platformio.ini`.

## Secrets

Several sketches use a local `secrets.h` file for WiFi credentials or bot tokens. Those files are intentionally ignored by Git through:

```gitignore
*/secrets.h
```

Do not commit real credentials. Use each sketch folder's local `secrets.h` or example file where provided.

## LoRa And Telegram Workflow

The current LoRa test flow is:

1. `lora-sender` generates dummy structured greenhouse data.
2. The payload is sent as CSV: `temp,humidity,moisture,insolation,motor_up,fan_on`.
3. `telegram-forwarder` receives the LoRa packet, logs sender address, RSSI, and SNR, then formats the payload into a Telegram message.

Current structured fields:

- temperature
- humidity
- soil moisture
- insolation
- motor-up state
- fan-on state

The forwarder falls back to forwarding raw text if the payload format does not parse.

## Board-Specific Notes

For the Arduino Nano ESP32:

- Built-in RGB LED aliases are `LEDR`, `LEDG`, and `LEDB`.
- The RGB LED is active-low.
- The default hardware serial aliases are `RX` and `TX`.
- The LoRa library reports `0` as a successful `checkStatus()` result.

The LoRa sender and forwarder currently use role colors on the built-in RGB LED and briefly blink green during send or receive activity.

## Adding A New Sketch

To add a new experiment:

1. Create a new top-level sketch folder with one `.ino` file.
2. Add a matching environment to `platformio.ini`.
3. Point that environment at the folder with `extra_scripts = pre:set_src_dir.py` and `custom_src_dir = <folder>`.
4. Add any library dependencies under that environment's `lib_deps`.
5. Add a local `secrets.h` if the sketch needs credentials.

This keeps experiments isolated while preserving Arduino IDE compatibility.

## Related Notes

Additional ESP32 notes live in `docs/README-ESP32.md`.