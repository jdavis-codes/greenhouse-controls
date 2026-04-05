---
name: platformio
description: PlatformIO workflow guidance for this repo, including using the local PlatformIO binary at ~/.platformio/penv/bin/pio for build, upload, test, and project tasks.
---

# PlatformIO Skill

Use this skill when the task involves PlatformIO commands (build, upload, test, clean, environment info) in this repository.

## Required binary

- Always use the explicit binary path: `~/.platformio/penv/bin/pio`.
- Do not assume `pio` is on PATH.
- If a command fails with option errors, re-check flags against `~/.platformio/penv/bin/pio -h`.

## Common commands

- List environments:
  ```bash
  ~/.platformio/penv/bin/pio project config
  ```

- Build (default env):
  ```bash
  ~/.platformio/penv/bin/pio run
  ```

- Build a typical active debug env (example):
  ```bash
  ~/.platformio/penv/bin/pio run -e bluebottle_idf_qtpy_debug
  ```

- Build a specific environment:
  ```bash
  ~/.platformio/penv/bin/pio run -e <env>
  ```

- Upload:
  ```bash
  ~/.platformio/penv/bin/pio run -t upload -e <env>
  ```

- Upload with explicit port:
  ```bash
  ~/.platformio/penv/bin/pio run -t upload -e <env> --upload-port /dev/cu.usbmodemXXXX
  ```

- Clean:
  ```bash
  ~/.platformio/penv/bin/pio run -t clean -e <env>
  ```

- Full clean:
  ```bash
  ~/.platformio/penv/bin/pio run -t fullclean -e <env>
  ```

- Tests:
  ```bash
  ~/.platformio/penv/bin/pio test -e <env>
  ```

- Native unit tests used by this repo:
  ```bash
  ~/.platformio/penv/bin/pio test -e native
  ```

## Clean build rules

- After changing any `sdkconfig.defaults`/board defaults, run clean/fullclean for the affected env before rebuilding.
- After changing partition CSV files, run fullclean before flashing to avoid stale partition artifacts.


## Example workflow

1. Read [platformio.ini](../../../platformio.ini) to find available environments.
2. Run `~/.platformio/penv/bin/pio run -e <env>`.
3. If upload is requested, run `~/.platformio/penv/bin/pio run -t upload -e <env>`.

---

## esptool (direct flashing of archived / merged binaries)

Use the PlatformIO-managed `esptool` binary at `~/.platformio/penv/bin/esptool`.

### Check version

```bash
~/.platformio/penv/bin/esptool version
```

### Flash a merged binary (WRONG — never do this directly)

Do **not** call `write-flash 0x0 <merged.bin>` on a flat merged binary. The binary is padded to flash size with `0xFF`, and the ESP32-S3 USB-Serial/JTAG controller will time out during the long padding-skip phase, causing a false failure at ~86%.

### Manual esptool commands (for debugging only)

Chip info / detected flash:
```bash
~/.platformio/penv/bin/esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX flash-id
```

Read flash image header (determine authoritative flash mode/freq/size):
```bash
~/.platformio/penv/bin/esptool image-info <path/to/firmware.bin>
```

Erase entire flash:
```bash
~/.platformio/penv/bin/esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX erase-flash
```

Flash individual segments (matches what PlatformIO does natively):
```bash
~/.platformio/penv/bin/esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  --baud 460800 --before default-reset --after hard-reset \
  write-flash \
    --flash-mode dio --flash-freq 80m --flash-size 8MB \
    0x0      .pio/build/<env>/bootloader.bin \
    0x8000   .pio/build/<env>/partitions.bin \
    0xe000   ~/.platformio/packages/framework-arduinoespressif32/.../ota_data_initial.bin \
    0x10000  .pio/build/<env>/firmware.bin
```
