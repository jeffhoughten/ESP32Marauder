#pragma once
#ifndef Rp2040Bridge_h
#define Rp2040Bridge_h

#include "configs.h"

// SenseCAP Indicator only: the GPS module and SD card are wired to the
// RP2040 companion MCU, not to the ESP32. A small bridge firmware on the
// RP2040 relays both over a single COBS-framed UART link. This class is
// the ESP32-side transport: it frames/deframes packets and dispatches the
// few packet types GpsInterface currently consumes. SD support needs the
// same transport plus request/response command helpers, which are not
// implemented yet (see configs.h HAS_SD comment for SenseCAP).
#ifdef MARAUDER_SENSECAP

#include <Arduino.h>

// Packet type IDs — must match the RP2040 bridge firmware's protocol.
#define RP2040_PKT_ACK          0x00
#define RP2040_PKT_PING         0x01
#define RP2040_PKT_PONG         0x02
#define RP2040_PKT_VERSION      0x03
#define RP2040_PKT_GPS_NMEA     0x50
#define RP2040_PKT_GPS_STATUS   0x51
#define RP2040_PKT_GPS_ENABLE   0x52
#define RP2040_PKT_SD_STATUS    0x60
#define RP2040_PKT_SD_OPEN      0x61
#define RP2040_PKT_SD_CLOSE     0x62
#define RP2040_PKT_SD_WRITE     0x63
#define RP2040_PKT_SD_READ      0x64
#define RP2040_PKT_SD_REMOVE    0x65
#define RP2040_PKT_SD_MKDIR     0x66
#define RP2040_PKT_SD_EXISTS    0x67
#define RP2040_PKT_SD_SIZE      0x68
#define RP2040_PKT_SD_LISTDIR   0x69
#define RP2040_PKT_SD_RESPONSE  0x6A
#define RP2040_PKT_SD_ERROR     0x6F

#define RP2040_UART_BAUD   921600
#define RP2040_MAX_PACKET  512

class Rp2040Bridge {
  public:
    // Opens the inter-chip UART. Uses GPS_TX/GPS_RX from configs.h for pin
    // numbers, following this codebase's existing GpsInterface convention
    // (the macro names the pin wired to the OTHER device's same-named pin).
    void begin();

    // Pumps the COBS decoder from whatever bytes are currently available
    // and dispatches any complete packets to the registered callbacks.
    // Call this frequently (every main-loop tick) — it never blocks.
    void poll();

    // Sends a COBS-framed command packet (cmd byte followed by payload).
    bool send(uint8_t cmd, const uint8_t* payload, size_t len);

    // Invoked once per decoded NMEA sentence. `sentence` is NOT
    // null-terminated and does not include the CR/LF the RP2040 stripped
    // off when it framed the sentence.
    void onGpsNmea(void (*cb)(const char* sentence, size_t len));

    // Invoked when the RP2040 reports whether a GPS module is physically
    // detected on its Grove UART.
    void onGpsStatus(void (*cb)(bool present));

  private:
    void handlePacket(const uint8_t* data, size_t len);

    uint8_t rx_raw[RP2040_MAX_PACKET * 2 + 8];
    size_t rx_raw_len = 0;

    void (*gps_nmea_cb)(const char*, size_t) = nullptr;
    void (*gps_status_cb)(bool) = nullptr;
};

extern Rp2040Bridge rp2040_bridge;

#endif // MARAUDER_SENSECAP
#endif
