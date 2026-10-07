/*
 * SenseCAP Indicator RP2040 Bridge Firmware
 * ==========================================
 * Bridges GPS and SD card peripherals on the RP2040 to the ESP32S3
 * running Marauder via the inter-chip UART using COBS framing.
 *
 * Hardware connections (SenseCAP Indicator):
 *   Inter-chip UART (to ESP32S3): TX=GPIO17, RX=GPIO16  (UART0)
 *   SD Card (SPI1):  SCK=GPIO10, MOSI=GPIO11, MISO=GPIO12, CS=GPIO13
 *   Grove I2C:       SDA=GPIO20, SCL=GPIO21
 *   Grove UART (GPS): TX=GPIO4, RX=GPIO5  (UART1, remapped to Grove pins)
 *   Sensor Power:    GPIO18
 *   Buzzer:          GPIO19
 *
 * Protocol: COBS-framed packets via PacketSerial
 *   Byte 0:    PKT_TYPE (command/response identifier)
 *   Byte 1-N:  Payload (varies by command)
 *
 * Board: "Seeed INDICATOR RP2040" (earlephilhower arduino-pico core)
 *
 * Required libraries:
 *   - PacketSerial (by Christopher Baker, v1.4.0+)
 *   - SD (built-in with arduino-pico)
 *
 * NOTE: This is a SEPARATE Arduino sketch/project from the ESP32 Marauder
 * firmware. It targets the RP2040 companion MCU on the SenseCAP Indicator
 * (board: "Seeed INDICATOR RP2040"), not the ESP32-S3. It is built and
 * flashed independently, over the RP2040's own USB connection, using the
 * earlephilhower arduino-pico core rather than arduino-esp32. Nothing in
 * the esp32_marauder/ sketch includes or compiles this file; the two
 * communicate only over the inter-chip UART described above. See
 * esp32_marauder/Rp2040Bridge.h for the ESP32-side half of this protocol.
 */

#include <PacketSerial.h>
#include <SPI.h>
#include <SD.h>
#include <SerialPIO.h>
// ============================================================
// Pin Definitions
// ============================================================
// Inter-chip UART to ESP32 (UART0)
// NOTE: These refer to the RP2040 hardware UART0 mux functions:
//   GPIO16 = UART0_TX (RP2040 transmits to ESP32 RX/GPIO19)
//   GPIO17 = UART0_RX (RP2040 receives from ESP32 TX/GPIO20)
// Do NOT swap these — they must match the silicon's UART0 pin mux.
#define ESP32_UART_TX   16
#define ESP32_UART_RX   17
#define ESP32_UART_BAUD 921600

// GPS module on Grove connector (UART1)
// Adjust these if your Grove port uses different pins
#define GPS_UART_TX     26   // RP2040 TX -> GPS RX
#define GPS_UART_RX     27   // RP2040 RX <- GPS TX
#define GPS_UART_BAUD   9600

// SD Card on SPI1
#define SD_SPI_SCK      10
#define SD_SPI_MOSI     11
#define SD_SPI_MISO     12
#define SD_CS           13

// Misc
#define SENSOR_POWER    18
#define BUZZER_PIN      19

// ============================================================
// Protocol Command Definitions
// ============================================================

// --- GPS Commands ---
#define PKT_GPS_NMEA        0x50  // RP2040 -> ESP32: raw NMEA sentence
#define PKT_GPS_STATUS      0x51  // RP2040 -> ESP32: GPS status (1=present, 0=absent)
#define PKT_GPS_ENABLE      0x52  // ESP32 -> RP2040: enable/disable GPS forwarding

// --- SD Card Commands ---
#define PKT_SD_STATUS       0x60  // Bidirectional: SD card status
#define PKT_SD_OPEN         0x61  // ESP32 -> RP2040: open file (success response is
                                   // [PKT_SD_RESPONSE, SD_OK, is_directory] -- 3 bytes,
                                   // not the plain 2-byte sendSdOk() other commands use)
#define PKT_SD_CLOSE        0x62  // ESP32 -> RP2040: close file
#define PKT_SD_WRITE         0x63  // ESP32 -> RP2040: write data to file
#define PKT_SD_READ         0x64  // ESP32 -> RP2040: read data from file
#define PKT_SD_REMOVE       0x65  // ESP32 -> RP2040: delete file
#define PKT_SD_MKDIR         0x66  // ESP32 -> RP2040: create directory
#define PKT_SD_RMDIR         0x6C  // ESP32 -> RP2040: remove empty directory
#define PKT_SD_EXISTS       0x67  // ESP32 -> RP2040: check if file exists
#define PKT_SD_SIZE          0x68  // ESP32 -> RP2040: get file size
#define PKT_SD_LISTDIR       0x69  // ESP32 -> RP2040: list directory
#define PKT_SD_RESPONSE     0x6A  // RP2040 -> ESP32: generic response with data
#define PKT_SD_RENAME        0x6B  // ESP32 -> RP2040: rename/move file
#define PKT_SD_ERROR         0x6F  // RP2040 -> ESP32: error response

// --- System Commands ---
#define PKT_ACK             0x00  // Bidirectional: acknowledgment
#define PKT_PING             0x01  // ESP32 -> RP2040: ping (are you alive?)
#define PKT_PONG             0x02  // RP2040 -> ESP32: pong response
#define PKT_VERSION         0x03  // RP2040 -> ESP32: firmware version string

// --- SD Open Mode Flags ---
#define SD_MODE_READ        0x00
#define SD_MODE_WRITE        0x01  // O_WRITE | O_CREAT | O_TRUNC
#define SD_MODE_APPEND       0x02  // O_WRITE | O_CREAT | O_APPEND

// --- SD Response Status Codes ---
#define SD_OK                 0x00
#define SD_ERR_NOT_MOUNTED  0x01
#define SD_ERR_OPEN_FAIL     0x02
#define SD_ERR_NO_FILE        0x03
#define SD_ERR_WRITE_FAIL     0x04
#define SD_ERR_READ_FAIL     0x05
#define SD_ERR_GENERIC       0xFF

// ============================================================
// Constants
// ============================================================
#define FIRMWARE_VERSION    "SCI-Bridge v0.2.2"
#define MAX_PACKET_SIZE     512
#define NMEA_BUFFER_SIZE    256
#define GPS_FORWARD_INTERVAL_MS  0   // 0 = forward every sentence immediately
#define SD_READ_CHUNK_SIZE  256      // Max bytes per SD read response

// ============================================================
// Globals
// ============================================================
PacketSerial_<COBS, 0, MAX_PACKET_SIZE> espSerial;

SerialPIO GPSUART(GPS_UART_TX,GPS_UART_RX);

// GPS state
bool gpsForwardingEnabled = true;
bool gpsDetected = false;
char nmeaBuffer[NMEA_BUFFER_SIZE];
uint16_t nmeaIdx = 0;

// SD state
bool sdMounted = false;
File openFile;
bool fileIsOpen = false;

// Transmit buffer
uint8_t txBuf[MAX_PACKET_SIZE];

// ============================================================
// Setup
// ============================================================
void setup() {
  // Debug serial (USB CDC)
  Serial.begin(115200);
  // Wait up to 3 seconds for USB serial monitor to connect.
  // If no monitor attached, we continue anyway after timeout.
  unsigned long waitStart = millis();
  while (!Serial && (millis() - waitStart < 3000)) {
    delay(10);
  }
  delay(200);  // Extra settle time for serial monitor
  Serial.println();
  Serial.println(FIRMWARE_VERSION);
  Serial.println("Initializing...");

  // Sensor/peripheral power ON
  // GPIO18 controls the power rail for onboard sensors AND the SD card slot.
  // Must be HIGH before attempting SD.begin().
  pinMode(SENSOR_POWER, OUTPUT);
  digitalWrite(SENSOR_POWER, HIGH);
  delay(100);  // Let power rail stabilize

  // Buzzer off
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ---- Inter-chip UART to ESP32 (UART0) ----
  Serial1.setTX(ESP32_UART_TX);
  Serial1.setRX(ESP32_UART_RX);
  Serial1.begin(ESP32_UART_BAUD);

  espSerial.setStream(&Serial1);
  espSerial.setPacketHandler(&onPacketReceived);

  // ---- GPS UART (UART1 via second hardware UART) ----
  // Comment out the next line if no GPS module is connected,
  // to avoid potential issues with unconnected UART pins.
  #define ENABLE_GPS_UART
  #ifdef ENABLE_GPS_UART
  GPSUART.begin(GPS_UART_BAUD);
  Serial.println("GPS UART initialized on pins 4/5");
  #else
  Serial.println("GPS UART disabled (no module connected)");
  gpsForwardingEnabled = false;
  #endif

  // ---- SD Card on SPI1 ----
  SPI1.setSCK(SD_SPI_SCK);
  SPI1.setTX(SD_SPI_MOSI);
  SPI1.setRX(SD_SPI_MISO);

  if (SD.begin(SD_CS, SPI1)) {
    sdMounted = true;
    Serial.println("SD card mounted OK");
  } else {
    sdMounted = false;
    Serial.println("SD card mount FAILED");
  }

  // Quick blink of buzzer as a "ready" indicator
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("Ready. Waiting for ESP32 commands...");
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
  // Process incoming COBS packets from ESP32
  espSerial.update();

  // Forward GPS NMEA sentences to ESP32
  if (gpsForwardingEnabled) {
    processGPS();
  }

  // Check USB serial for debug commands
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      Serial.println("Rebooting RP2040...");
      delay(100);
      rp2040.reboot();
    }
  }
}

// ============================================================
// GPS Processing
// ============================================================
void processGPS() {
  while (GPSUART.available()) {
    char c = GPSUART.read();

    if (c == '$') {
      // Start of a new NMEA sentence
      nmeaIdx = 0;
      nmeaBuffer[nmeaIdx++] = c;
    } else if (c == '\n' || c == '\r') {
      if (nmeaIdx > 1) {
        // Complete NMEA sentence received - forward it
        nmeaBuffer[nmeaIdx] = '\0';

        if (!gpsDetected) {
          gpsDetected = true;
          sendGpsStatus(1);
          Serial.println("GPS module detected");
        }

        sendNmeaSentence(nmeaBuffer, nmeaIdx);
        nmeaIdx = 0;
      }
    } else {
      if (nmeaIdx < NMEA_BUFFER_SIZE - 1) {
        nmeaBuffer[nmeaIdx++] = c;
      } else {
        // Buffer overflow, discard
        nmeaIdx = 0;
      }
    }
  }
}

void sendNmeaSentence(const char* sentence, uint16_t len) {
  if (len + 1 > MAX_PACKET_SIZE) return;
  txBuf[0] = PKT_GPS_NMEA;
  memcpy(&txBuf[1], sentence, len);
  espSerial.send(txBuf, len + 1);
}

void sendGpsStatus(uint8_t status) {
  txBuf[0] = PKT_GPS_STATUS;
  txBuf[1] = status;
  espSerial.send(txBuf, 2);
}

// ============================================================
// Packet Handler - Commands from ESP32
// ============================================================
void onPacketReceived(const uint8_t* buffer, size_t size) {
  if (size < 1) return;

  uint8_t cmd = buffer[0];

  // Debug: print every received packet
  Serial.print("[RX] cmd=0x");
  if (cmd < 0x10) Serial.print("0");
  Serial.print(cmd, HEX);
  Serial.print(" size=");
  Serial.print(size);
  Serial.print(" data:");
  for (size_t i = 0; i < size && i < 16; i++) {
    Serial.print(" 0x");
    if (buffer[i] < 0x10) Serial.print("0");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();

  switch (cmd) {
    case PKT_PING:
      handlePing();
      break;

    case PKT_GPS_ENABLE:
      if (size >= 2) {
        gpsForwardingEnabled = (buffer[1] != 0);
        Serial.print("GPS forwarding: ");
        Serial.println(gpsForwardingEnabled ? "ON" : "OFF");
        sendAck();
      }
      break;

    case PKT_SD_STATUS:
      handleSdStatus();
      break;

    case PKT_SD_OPEN:
      if (size >= 3) {
        handleSdOpen(buffer + 1, size - 1);
      }
      break;

    case PKT_SD_CLOSE:
      handleSdClose();
      break;

    case PKT_SD_WRITE:
      if (size >= 2) {
        handleSdWrite(buffer + 1, size - 1);
      }
      break;

    case PKT_SD_READ:
      if (size >= 3) {
        handleSdRead(buffer + 1, size - 1);
      }
      break;

    case PKT_SD_REMOVE:
      if (size >= 2) {
        handleSdRemove(buffer + 1, size - 1);
      }
      break;

    case PKT_SD_MKDIR:
      if (size >= 2) {
        handleSdMkdir(buffer + 1, size - 1);
      }
      break;

    case PKT_SD_RMDIR:
      if (size >= 2) {
        handleSdRmdir(buffer + 1, size - 1);
      }
      break;

    case PKT_SD_EXISTS:
      if (size >= 2) {
        handleSdExists(buffer + 1, size - 1);
      }
      break;

    case PKT_SD_SIZE:
      handleSdFileSize();
      break;

    case PKT_SD_LISTDIR:
      if (size >= 2) {
        handleSdListDir(buffer + 1, size - 1);
      }
      break;

    case PKT_SD_RENAME:
      if (size >= 3) {
        handleSdRename(buffer + 1, size - 1);
      }
      break;

    default:
      Serial.print("Unknown command: 0x");
      Serial.println(cmd, HEX);
      break;
  }
}

// ============================================================
// System Command Handlers
// ============================================================
void handlePing() {
  Serial.println("[handlePing] Building PONG response...");
  txBuf[0] = PKT_PONG;
  uint8_t vlen = strlen(FIRMWARE_VERSION);
  memcpy(&txBuf[1], FIRMWARE_VERSION, vlen);
  Serial.print("[handlePing] Sending PONG, ");
  Serial.print(1 + vlen);
  Serial.println(" bytes");
  espSerial.send(txBuf, 1 + vlen);
  Serial.println("[handlePing] PONG sent");
}

void sendAck() {
  txBuf[0] = PKT_ACK;
  espSerial.send(txBuf, 1);
}

void sendSdError(uint8_t errCode) {
  txBuf[0] = PKT_SD_ERROR;
  txBuf[1] = errCode;
  espSerial.send(txBuf, 2);
}

void sendSdOk() {
  txBuf[0] = PKT_SD_RESPONSE;
  txBuf[1] = SD_OK;
  espSerial.send(txBuf, 2);
}

// ============================================================
// SD Card Command Handlers
// ============================================================

void handleSdStatus() {
  // Try to re-mount if not mounted
  if (!sdMounted) {
    sdMounted = SD.begin(SD_CS, SPI1);
  }
  txBuf[0] = PKT_SD_STATUS;
  txBuf[1] = sdMounted ? 1 : 0;
  espSerial.send(txBuf, 2);
  Serial.print("SD Status: ");
  Serial.println(sdMounted ? "mounted" : "not mounted");
}

void handleSdOpen(const uint8_t* payload, size_t len) {
  // Payload: [mode_byte] [filename_string...]
  if (!sdMounted) {
    sendSdError(SD_ERR_NOT_MOUNTED);
    return;
  }

  // Close any previously open file
  if (fileIsOpen) {
    openFile.close();
    fileIsOpen = false;
  }

  uint8_t mode = payload[0];
  // Extract null-terminated filename
  char filename[128];
  size_t fnLen = len - 1;
  if (fnLen >= sizeof(filename)) fnLen = sizeof(filename) - 1;
  memcpy(filename, &payload[1], fnLen);
  filename[fnLen] = '\0';

  Serial.print("SD OPEN: ");
  Serial.print(filename);
  Serial.print(" mode=");
  Serial.println(mode);

  switch (mode) {
    case SD_MODE_READ:
      openFile = SD.open(filename, FILE_READ);
      break;
    case SD_MODE_WRITE:
      openFile = SD.open(filename, FILE_WRITE);
      break;
    case SD_MODE_APPEND:
      // arduino-pico SD: FILE_WRITE already appends by default
      // For true truncation in WRITE mode above, we remove first
      openFile = SD.open(filename, FILE_WRITE);
      break;
    default:
      openFile = SD.open(filename, FILE_READ);
      break;
  }

  if (openFile) {
    fileIsOpen = true;
    // Custom 3-byte success response (not the shared sendSdOk()): callers
    // need to know immediately whether what they opened is a directory,
    // without a second round-trip, since SD.open() succeeds for both.
    txBuf[0] = PKT_SD_RESPONSE;
    txBuf[1] = SD_OK;
    txBuf[2] = openFile.isDirectory() ? 1 : 0;
    espSerial.send(txBuf, 3);
    Serial.println("  -> opened OK");
  } else {
    sendSdError(SD_ERR_OPEN_FAIL);
    Serial.println("  -> FAILED");
  }
}

void handleSdClose() {
  if (fileIsOpen) {
    openFile.close();
    fileIsOpen = false;
    Serial.println("SD CLOSE: OK");
  }
  sendSdOk();
}

void handleSdWrite(const uint8_t* payload, size_t len) {
  // Payload is raw data bytes to write
  if (!sdMounted) { sendSdError(SD_ERR_NOT_MOUNTED); return; }
  if (!fileIsOpen) { sendSdError(SD_ERR_NO_FILE); return; }

  size_t written = openFile.write(payload, len);
  openFile.flush();

  if (written == len) {
    sendSdOk();
  } else {
    sendSdError(SD_ERR_WRITE_FAIL);
    Serial.print("SD WRITE: wanted ");
    Serial.print(len);
    Serial.print(" wrote ");
    Serial.println(written);
  }
}

void handleSdRead(const uint8_t* payload, size_t len) {
  // Payload: [2 bytes: requested_length (little-endian)]
  if (!sdMounted) { sendSdError(SD_ERR_NOT_MOUNTED); return; }
  if (!fileIsOpen) { sendSdError(SD_ERR_NO_FILE); return; }

  uint16_t reqLen = payload[0] | (payload[1] << 8);
  if (reqLen > SD_READ_CHUNK_SIZE) reqLen = SD_READ_CHUNK_SIZE;

  // txBuf[0] = PKT_SD_RESPONSE, txBuf[1] = status, txBuf[2..] = data
  uint8_t readBuf[SD_READ_CHUNK_SIZE];
  int bytesRead = openFile.read(readBuf, reqLen);

  if (bytesRead < 0) {
    sendSdError(SD_ERR_READ_FAIL);
    return;
  }

  txBuf[0] = PKT_SD_RESPONSE;
  txBuf[1] = SD_OK;
  txBuf[2] = bytesRead & 0xFF;
  txBuf[3] = (bytesRead >> 8) & 0xFF;
  if (bytesRead > 0) {
    memcpy(&txBuf[4], readBuf, bytesRead);
  }
  espSerial.send(txBuf, 4 + bytesRead);
}

void handleSdRemove(const uint8_t* payload, size_t len) {
  if (!sdMounted) { sendSdError(SD_ERR_NOT_MOUNTED); return; }

  char filename[128];
  if (len >= sizeof(filename)) len = sizeof(filename) - 1;
  memcpy(filename, payload, len);
  filename[len] = '\0';

  if (SD.remove(filename)) {
    sendSdOk();
    Serial.print("SD REMOVE: ");
    Serial.println(filename);
  } else {
    sendSdError(SD_ERR_GENERIC);
  }
}

void handleSdMkdir(const uint8_t* payload, size_t len) {
  if (!sdMounted) { sendSdError(SD_ERR_NOT_MOUNTED); return; }

  char dirname[128];
  if (len >= sizeof(dirname)) len = sizeof(dirname) - 1;
  memcpy(dirname, payload, len);
  dirname[len] = '\0';

  if (SD.mkdir(dirname)) {
    sendSdOk();
    Serial.print("SD MKDIR: ");
    Serial.println(dirname);
  } else {
    sendSdError(SD_ERR_GENERIC);
  }
}

void handleSdRmdir(const uint8_t* payload, size_t len) {
  if (!sdMounted) { sendSdError(SD_ERR_NOT_MOUNTED); return; }

  char dirname[128];
  if (len >= sizeof(dirname)) len = sizeof(dirname) - 1;
  memcpy(dirname, payload, len);
  dirname[len] = '\0';

  if (SD.rmdir(dirname)) {
    sendSdOk();
    Serial.print("SD RMDIR: ");
    Serial.println(dirname);
  } else {
    sendSdError(SD_ERR_GENERIC);
  }
}

void handleSdExists(const uint8_t* payload, size_t len) {
  if (!sdMounted) { sendSdError(SD_ERR_NOT_MOUNTED); return; }

  char filename[128];
  if (len >= sizeof(filename)) len = sizeof(filename) - 1;
  memcpy(filename, payload, len);
  filename[len] = '\0';

  txBuf[0] = PKT_SD_RESPONSE;
  txBuf[1] = SD_OK;
  txBuf[2] = SD.exists(filename) ? 1 : 0;
  espSerial.send(txBuf, 3);
}

void handleSdFileSize() {
  if (!fileIsOpen) { sendSdError(SD_ERR_NO_FILE); return; }

  uint32_t sz = openFile.size();
  txBuf[0] = PKT_SD_RESPONSE;
  txBuf[1] = SD_OK;
  txBuf[2] = sz & 0xFF;
  txBuf[3] = (sz >> 8) & 0xFF;
  txBuf[4] = (sz >> 16) & 0xFF;
  txBuf[5] = (sz >> 24) & 0xFF;
  espSerial.send(txBuf, 6);
}

void handleSdListDir(const uint8_t* payload, size_t len) {
  if (!sdMounted) { sendSdError(SD_ERR_NOT_MOUNTED); return; }

  char dirname[128];
  if (len >= sizeof(dirname)) len = sizeof(dirname) - 1;
  memcpy(dirname, payload, len);
  dirname[len] = '\0';

  File dir = SD.open(dirname);
  if (!dir || !dir.isDirectory()) {
    sendSdError(SD_ERR_OPEN_FAIL);
    return;
  }

  // Send each entry as a separate response packet
  // Format: PKT_SD_RESPONSE, SD_OK, [is_dir_byte], [filename_string]
  File entry;
  while ((entry = dir.openNextFile())) {
    const char* name = entry.name();
    uint8_t nameLen = strlen(name);
    txBuf[0] = PKT_SD_RESPONSE;
    txBuf[1] = SD_OK;
    txBuf[2] = entry.isDirectory() ? 1 : 0;
    // 4 bytes file size
    uint32_t sz = entry.size();
    txBuf[3] = sz & 0xFF;
    txBuf[4] = (sz >> 8) & 0xFF;
    txBuf[5] = (sz >> 16) & 0xFF;
    txBuf[6] = (sz >> 24) & 0xFF;
    memcpy(&txBuf[7], name, nameLen);
    espSerial.send(txBuf, 7 + nameLen);
    entry.close();
  }
  dir.close();

  // Send end-of-list marker
  txBuf[0] = PKT_SD_RESPONSE;
  txBuf[1] = SD_OK;
  txBuf[2] = 0xFF;  // sentinel: end of listing
  espSerial.send(txBuf, 3);
}

void handleSdRename(const uint8_t* payload, size_t len) {
  // Payload: [fromLen: 1 byte] [fromPath bytes...] [toPath bytes... (rest)]
  if (!sdMounted) { sendSdError(SD_ERR_NOT_MOUNTED); return; }
  if (len < 2) { sendSdError(SD_ERR_GENERIC); return; }

  uint8_t fromLen = payload[0];
  if (fromLen == 0 || (size_t)(1 + fromLen) >= len) {
    // Need at least one byte left over for the (non-empty) "to" path.
    sendSdError(SD_ERR_GENERIC);
    return;
  }

  char fromPath[128];
  char toPath[128];
  size_t toLen = len - 1 - fromLen;

  size_t copyFromLen = fromLen;
  if (copyFromLen >= sizeof(fromPath)) copyFromLen = sizeof(fromPath) - 1;
  memcpy(fromPath, &payload[1], copyFromLen);
  fromPath[copyFromLen] = '\0';

  size_t copyToLen = toLen;
  if (copyToLen >= sizeof(toPath)) copyToLen = sizeof(toPath) - 1;
  memcpy(toPath, &payload[1 + fromLen], copyToLen);
  toPath[copyToLen] = '\0';

  // Renaming a file out from under an open handle would leave `openFile`
  // pointing at a stale path -- close it first if it might be the same file.
  // (arduino-pico's File has no path()/name() accessor cheap enough to
  // compare reliably, so just always close rather than risk it.)
  if (fileIsOpen) {
    openFile.close();
    fileIsOpen = false;
  }

  if (SD.rename(fromPath, toPath)) {
    sendSdOk();
    Serial.print("SD RENAME: ");
    Serial.print(fromPath);
    Serial.print(" -> ");
    Serial.println(toPath);
  } else {
    sendSdError(SD_ERR_GENERIC);
    Serial.print("SD RENAME FAILED: ");
    Serial.print(fromPath);
    Serial.print(" -> ");
    Serial.println(toPath);
  }
}
