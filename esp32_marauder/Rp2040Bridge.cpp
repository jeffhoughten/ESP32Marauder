#include "Rp2040Bridge.h"

#ifdef MARAUDER_SENSECAP

#include <string.h>

Rp2040Bridge rp2040_bridge;

// ---- COBS (Consistent Overhead Byte Stuffing) ----
// Matches the encoding the RP2040 bridge firmware's PacketSerial library
// uses: data is stuffed so no 0x00 byte appears except as the frame
// delimiter written by the caller.

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

void Rp2040Bridge::begin() {
  // GPS_TX/GPS_RX for MARAUDER_SENSECAP are the ESP32-side pins of the
  // inter-chip UART to the RP2040 (see configs.h), following this
  // codebase's existing convention of naming each pin after the OTHER
  // device's matching pin.
  Serial2.begin(RP2040_UART_BAUD, SERIAL_8N1, GPS_TX, GPS_RX);
  this->rx_raw_len = 0;
}

void Rp2040Bridge::poll() {
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

void Rp2040Bridge::handlePacket(const uint8_t* data, size_t len) {
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
      // PONG / ACK / VERSION / SD_* — nothing consumes these yet.
      break;
  }
}

bool Rp2040Bridge::send(uint8_t cmd, const uint8_t* payload, size_t len) {
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

void Rp2040Bridge::onGpsNmea(void (*cb)(const char*, size_t)) {
  this->gps_nmea_cb = cb;
}

void Rp2040Bridge::onGpsStatus(void (*cb)(bool)) {
  this->gps_status_cb = cb;
}

#endif // MARAUDER_SENSECAP
