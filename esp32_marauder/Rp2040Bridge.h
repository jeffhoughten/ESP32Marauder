#pragma once
#ifndef Rp2040Bridge_h
#define Rp2040Bridge_h

#include "configs.h"

// SenseCAP Indicator only: the GPS module and SD card are wired to the
// RP2040 companion MCU, not the ESP32. A small bridge firmware on the
// RP2040 relays both over a single COBS-framed UART link. This class is
// the ESP32-side transport: it frames/deframes packets and dispatches the
// few packet types GpsInterface currently consumes. SD support needs the
// same transport plus request/response command helpers, which are not
// implemented yet (see configs.h HAS_SD comment for SenseCAP).
//
// Header-only (no matching .cpp): every method is defined inline here so
// this single file is both the declaration and the implementation,
// matching the RP2040-side bridge firmware's single-file structure.
// `inline` (on methods and the global instance below) means the linker
// collapses the copy each .cpp that #includes this file generates down
// to one definition, so there's no one-definition-rule violation despite
// multiple translation units including it.
#ifdef MARAUDER_SENSECAP

#include <Arduino.h>
#include <string.h>

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
#define RP2040_PKT_SD_RENAME    0x6B
#define RP2040_PKT_SD_RMDIR     0x6C
#define RP2040_PKT_SD_ERROR     0x6F

#define RP2040_UART_BAUD   921600
#define RP2040_MAX_PACKET  512

class Rp2040Bridge {
  public:
    // Opens the inter-chip UART. Uses GPS_TX/GPS_RX from configs.h for pin
    // numbers, following this codebase's existing GpsInterface convention
    // (the macro names the pin wired to the OTHER device's same-named pin).
    void begin() {
      Serial2.begin(RP2040_UART_BAUD, SERIAL_8N1, GPS_TX, GPS_RX);
      this->rx_raw_len = 0;
    }

    // Pumps the COBS decoder from whatever bytes are currently available
    // and dispatches any complete packets to the registered callbacks.
    // Call this frequently (every main-loop tick) — it never blocks.
    void poll() {
      while (Serial2.available()) {
        uint8_t b = (uint8_t)Serial2.read();

        if (b == 0x00) {
          if (this->rx_raw_len > 0) {
            uint8_t decoded[RP2040_MAX_PACKET];
            size_t decoded_len = cobsDecode(this->rx_raw, this->rx_raw_len, decoded, sizeof(decoded));
            if (decoded_len > 0) {
              this->handlePacket(decoded, decoded_len);
            }
          }
          this->rx_raw_len = 0;
        } else if (this->rx_raw_len < sizeof(this->rx_raw)) {
          this->rx_raw[this->rx_raw_len++] = b;
        } else {
          // Overflowed without seeing a delimiter — drop the frame so far.
          this->rx_raw_len = 0;
        }
      }
    }

    // Sends a COBS-framed command packet (cmd byte followed by payload).
    bool send(uint8_t cmd, const uint8_t* payload, size_t len) {
      if (len + 1 > RP2040_MAX_PACKET) return false;

      uint8_t raw[RP2040_MAX_PACKET];
      raw[0] = cmd;
      if (len > 0 && payload != nullptr) {
        memcpy(&raw[1], payload, len);
      }

      uint8_t encoded[RP2040_MAX_PACKET * 2 + 8];
      size_t encoded_len = cobsEncode(raw, len + 1, encoded, sizeof(encoded));
      if (encoded_len == 0) return false;

      Serial2.write(encoded, encoded_len);
      Serial2.write((uint8_t)0x00);
      return true;
    }

    // Invoked once per decoded NMEA sentence. `sentence` is NOT
    // null-terminated and does not include the CR/LF the RP2040 stripped
    // off when it framed the sentence.
    void onGpsNmea(void (*cb)(const char* sentence, size_t len)) {
      this->gps_nmea_cb = cb;
    }

    // Invoked when the RP2040 reports whether a GPS module is physically
    // detected on its Grove UART.
    void onGpsStatus(void (*cb)(bool present)) {
      this->gps_status_cb = cb;
    }

    // Invoked for every packet type not already claimed by onGpsNmea/
    // onGpsStatus above -- currently just the SD_* and PONG/ACK/VERSION
    // types. `payload`/`len` exclude the leading command byte (`cmd`).
    // Used by the SD filesystem backend (Rp2040SdFs.h) to capture
    // request/response traffic; pass nullptr to clear it. Only one
    // registration at a time -- fine, since SD requests are answered
    // synchronously one at a time by design (see Rp2040SdFs.h).
    void onOther(void (*cb)(uint8_t cmd, const uint8_t* payload, size_t len)) {
      this->other_cb = cb;
    }

  private:
    // ---- COBS (Consistent Overhead Byte Stuffing) ----
    // Matches the encoding the RP2040 bridge firmware's PacketSerial
    // library uses: data is stuffed so no 0x00 byte appears except as the
    // frame delimiter written by the caller. `static` keeps these helpers
    // privately linked per translation unit — harmless duplication, and
    // simpler than threading `inline` through free functions.
    static size_t cobsEncode(const uint8_t* src, size_t len, uint8_t* dst, size_t dst_cap) {
      size_t read = 0, write = 1, code_idx = 0;
      uint8_t code = 1;

      if (dst_cap < 1) return 0;

      while (read < len) {
        if (write >= dst_cap) return 0;

        if (src[read] == 0) {
          dst[code_idx] = code;
          code = 1;
          code_idx = write++;
          read++;
        } else {
          dst[write++] = src[read++];
          code++;
          if (code == 0xFF) {
            if (write >= dst_cap) return 0;
            dst[code_idx] = code;
            code = 1;
            code_idx = write++;
          }
        }
      }

      dst[code_idx] = code;
      return write;
    }

    static size_t cobsDecode(const uint8_t* src, size_t len, uint8_t* dst, size_t dst_cap) {
      size_t read = 0, write = 0;

      while (read < len) {
        uint8_t code = src[read++];

        for (uint8_t i = 1; i < code; i++) {
          if (read >= len || write >= dst_cap) return 0; // malformed/truncated frame
          dst[write++] = src[read++];
        }

        if (code != 0xFF && read < len) {
          if (write >= dst_cap) return 0;
          dst[write++] = 0;
        }
      }

      return write;
    }

    void handlePacket(const uint8_t* data, size_t len) {
      if (len < 1) return;

      switch (data[0]) {
        case RP2040_PKT_GPS_NMEA:
          if (this->gps_nmea_cb) {
            this->gps_nmea_cb((const char*)(data + 1), len - 1);
          }
          break;

        case RP2040_PKT_GPS_STATUS:
          if (len >= 2 && this->gps_status_cb) {
            this->gps_status_cb(data[1] != 0);
          }
          break;

        default:
          // PONG / ACK / VERSION / SD_* — handed to onOther() if anyone's
          // listening (the SD backend), ignored otherwise.
          if (this->other_cb) {
            this->other_cb(data[0], data + 1, len - 1);
          }
          break;
      }
    }

    uint8_t rx_raw[RP2040_MAX_PACKET * 2 + 8];
    size_t rx_raw_len = 0;

    void (*gps_nmea_cb)(const char*, size_t) = nullptr;
    void (*gps_status_cb)(bool) = nullptr;
    void (*other_cb)(uint8_t, const uint8_t*, size_t) = nullptr;
};

// C++17 inline variable: every .cpp that #includes this header shares the
// same one definition instead of each getting its own (which would be a
// one-definition-rule violation for a plain global). This codebase's
// toolchain already relies on the same feature elsewhere (see the
// `inline bool` globals in WiFiScan.h), so it's established precedent.
inline Rp2040Bridge rp2040_bridge;

#endif // MARAUDER_SENSECAP
#endif
