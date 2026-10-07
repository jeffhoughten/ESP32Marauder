# RP2040 Bridge Firmware

This is the firmware for the **RP2040 companion MCU** on the SenseCAP
Indicator — a completely separate Arduino project from the ESP32 Marauder
firmware in `esp32_marauder/`. It runs on a different chip, is built with
a different board target, and is flashed independently over the RP2040's
own USB connection.

| | This project | `esp32_marauder/` |
|---|---|---|
| Chip | RP2040 (companion MCU) | ESP32-S3 (runs Marauder) |
| Board (Arduino IDE) | "Seeed INDICATOR RP2040" | ESP32S3 Dev Module (or similar) |
| Core | earlephilhower arduino-pico | arduino-esp32 |
| Libraries needed | PacketSerial (Christopher Baker, v1.4.0+), SD (bundled with arduino-pico) | see main repo |

It owns the GPS module and SD card slot, which are wired to the RP2040, not
the ESP32, and relays both to the ESP32 over a dedicated inter-chip UART
(921600 baud) using COBS-framed packets. See `esp32_marauder/Rp2040Bridge.h`
for the ESP32-side client that speaks this same protocol — the packet type
IDs and payload formats in both files must stay in sync; if you change one,
change the other.

To build and flash: open `rp2040_bridge.ino` in Arduino IDE, select the
"Seeed INDICATOR RP2040" board, install the two libraries above, and
upload over the RP2040's USB port (separate from the ESP32's USB port).
