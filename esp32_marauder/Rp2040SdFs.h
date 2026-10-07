#pragma once
#ifndef Rp2040SdFs_h
#define Rp2040SdFs_h

#include "configs.h"

// SenseCAP Indicator only: the SD card is wired to the RP2040 companion
// MCU, not the ESP32 (see Rp2040Bridge.h and rp2040_bridge/README.md).
// This file implements fs::FSImpl/fs::FileImpl backed by that bridge's
// PKT_SD_* request/response commands, and provides a global `SD` of type
// fs::FS built on it -- a genuine drop-in for every fs::FS&/fs::FS*/File
// usage elsewhere in this codebase (SDInterface.cpp, WiFiScan.cpp,
// Buffer.cpp, Settings.cpp, ...), none of which needed any changes.
//
// This is included INSTEAD of the real <SD.h> (see the swap in
// SDInterface.h) -- both define a global named `SD`, so only one of the
// two headers can ever be included in a given build.
//
// Two real protocol constraints this implementation works around:
//
// 1. One open file at a time. The RP2040 firmware tracks a single
//    `File openFile`, not a table of handles -- opening a new one
//    silently closes whatever was open before. A global "generation"
//    counter, bumped on every successful open/close, lets each FileImpl
//    detect it's been invalidated by a later open and fail safe (return
//    0/false) instead of silently operating on the wrong file.
// 2. No per-handle directory iteration. PKT_SD_LISTDIR is a separate,
//    self-contained command that streams an entire directory's entries
//    in one round trip -- it doesn't interact with the single open-file
//    slot at all. So a directory FileImpl fetches its full listing once,
//    eagerly, at open time, and serves openNextFile()/getNextFileName()
//    from that cached list. Every actual call site in this codebase
//    (removeTree/copyTree in SDInterface.cpp -- nothing else calls
//    openNextFile() at all) only ever reads path()/name()/isDirectory()
//    off each child and closes it, never reads/writes it directly, so
//    children are lightweight metadata-only handles that never touch the
//    wire themselves.
#ifdef MARAUDER_SENSECAP

#include "Rp2040Bridge.h"
#include <FS.h>
#include <FSImpl.h>  // FS.h only forward-declares fs::FileImpl/fs::FSImpl; the
                      // full virtual interfaces this subclasses live here.
#include <LinkedList.h>

// ============================================================
// Blocking request/response transport for the SD half of the protocol.
// ============================================================
// The bridge protocol is inherently one-request-at-a-time for SD (see
// constraint 1 above), so this is deliberately synchronous: send one
// command, then block -- pumping rp2040_bridge.poll(), which keeps
// dispatching any interleaved GPS packets normally through its own
// callbacks -- until a matching response arrives or `timeout_ms` elapses.
class Rp2040SdTransport {
  public:
    // Set by request()/listDir() on return; read these immediately after
    // a request() call before issuing another one.
    static inline uint8_t resp_cmd = 0;
    static inline uint8_t resp_buf[RP2040_MAX_PACKET] = {};
    static inline size_t resp_len = 0;

    // Sends `cmd` + payload, blocks for a single response packet (one of
    // SD_RESPONSE / SD_ERROR / SD_STATUS). Returns false on send failure
    // or timeout; true means resp_cmd/resp_buf/resp_len are valid --
    // callers still need to check resp_cmd themselves to know whether it
    // was success or an error response.
    static bool request(uint8_t cmd, const uint8_t* payload, size_t len, uint32_t timeout_ms = 4000) {
      pending = true;
      resp_len = 0;
      resp_cmd = 0;
      rp2040_bridge.onOther(&Rp2040SdTransport::captureOne);
      bool sent = rp2040_bridge.send(cmd, payload, len);
      if (!sent) {
        rp2040_bridge.onOther(nullptr);
        pending = false;
        return false;
      }
      uint32_t start = millis();
      while (pending && (millis() - start) < timeout_ms) {
        rp2040_bridge.poll();
      }
      rp2040_bridge.onOther(nullptr);
      return !pending;
    }

    // Lists a directory in one round trip, appending each entry it
    // receives to `out` (does not clear it first). Returns false on
    // error/timeout/send-failure; whatever entries arrived before that
    // point are still left in `out`.
    struct DirEntry {
      String name;
      uint32_t size;
      bool is_dir;
    };

    static bool listDir(const char* path, LinkedList<DirEntry>* out, uint32_t timeout_ms = 8000) {
      listdir_out = out;
      listdir_done = false;
      listdir_ok = false;
      rp2040_bridge.onOther(&Rp2040SdTransport::captureListDir);
      size_t path_len = strlen(path);
      bool sent = rp2040_bridge.send(RP2040_PKT_SD_LISTDIR, (const uint8_t*)path, path_len);
      if (!sent) {
        rp2040_bridge.onOther(nullptr);
        return false;
      }
      uint32_t start = millis();
      while (!listdir_done && (millis() - start) < timeout_ms) {
        rp2040_bridge.poll();
      }
      rp2040_bridge.onOther(nullptr);
      return listdir_done && listdir_ok;
    }

  private:
    static inline volatile bool pending = false;

    // payload/len exclude the leading command byte (Rp2040Bridge's
    // onOther() contract) -- see each PKT_SD_* command's response layout
    // documented in rp2040_bridge/rp2040_bridge.ino.
    static void captureOne(uint8_t cmd, const uint8_t* payload, size_t len) {
      if (cmd != RP2040_PKT_SD_RESPONSE && cmd != RP2040_PKT_SD_ERROR && cmd != RP2040_PKT_SD_STATUS) {
        return;  // GPS traffic interleaved on the same link -- not ours.
      }
      resp_cmd = cmd;
      resp_len = (len > sizeof(resp_buf)) ? sizeof(resp_buf) : len;
      if (resp_len > 0) {
        memcpy(resp_buf, payload, resp_len);
      }
      pending = false;
    }

    static inline LinkedList<DirEntry>* listdir_out = nullptr;
    static inline volatile bool listdir_done = false;
    static inline bool listdir_ok = false;

    static void captureListDir(uint8_t cmd, const uint8_t* payload, size_t len) {
      if (cmd == RP2040_PKT_SD_ERROR) {
        listdir_ok = false;
        listdir_done = true;
        return;
      }
      if (cmd != RP2040_PKT_SD_RESPONSE || len < 2) {
        return;  // GPS traffic, or too short to even hold the sentinel.
      }
      // payload[0] = SD_OK status (always OK if the RP2040 sent this at
      // all), payload[1] = is_directory, or 0xFF as the end-of-list marker.
      uint8_t is_dir_or_sentinel = payload[1];
      if (is_dir_or_sentinel == 0xFF) {
        listdir_ok = true;
        listdir_done = true;
        return;
      }
      if (len < 6) return;  // malformed: need 4 more size bytes before the name
      uint32_t sz = (uint32_t)payload[2] | ((uint32_t)payload[3] << 8) | ((uint32_t)payload[4] << 16) | ((uint32_t)payload[5] << 24);
      size_t name_len = len - 6;
      char name_buf[128];
      if (name_len >= sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
      memcpy(name_buf, payload + 6, name_len);
      name_buf[name_len] = '\0';
      if (listdir_out) {
        DirEntry e;
        e.name = String(name_buf);
        e.size = sz;
        e.is_dir = (is_dir_or_sentinel != 0);
        listdir_out->add(e);
      }
    }
};

// ============================================================
// FileImpl
// ============================================================
class Rp2040SdFileImpl : public fs::FileImpl {
  public:
    enum class Kind { Closed, RealFile, Directory, StatOnly };

    // Real file or directory, just opened via PKT_SD_OPEN.
    // `generation` is the g_open_generation value as of this open --
    // compared against the live counter before every wire operation to
    // detect a later open having silently invalidated this handle.
    Rp2040SdFileImpl(const String& path, bool open_ok, bool is_directory, uint32_t generation)
      : _path(path), _generation(generation) {
      if (!open_ok) {
        _kind = Kind::Closed;
      } else if (is_directory) {
        _kind = Kind::Directory;
        Rp2040SdTransport::listDir(_path.c_str(), &_entries);
      } else {
        _kind = Kind::RealFile;
      }
    }

    // Lightweight metadata-only entry from a parent directory's cached
    // listing. Never touches the wire -- see the class-level comment on
    // why this is safe given this codebase's actual usage.
    Rp2040SdFileImpl(const String& path, const String& name, uint32_t size, bool is_dir)
      : _path(path), _name(name), _kind(Kind::StatOnly), _statSize(size), _statIsDir(is_dir) {}

    ~Rp2040SdFileImpl() override {
      close();
    }

    size_t write(const uint8_t* buf, size_t size) override {
      if (!staleCheck()) return 0;
      size_t total = 0;
      while (total < size) {
        size_t chunk = size - total;
        if (chunk > 256) chunk = 256;
        if (!Rp2040SdTransport::request(RP2040_PKT_SD_WRITE, buf + total, chunk)) break;
        if (Rp2040SdTransport::resp_cmd != RP2040_PKT_SD_RESPONSE) break;
        total += chunk;
      }
      return total;
    }

    size_t read(uint8_t* buf, size_t size) override {
      if (!staleCheck()) return 0;
      size_t total = 0;
      while (total < size) {
        size_t want = size - total;
        if (want > 256) want = 256;
        uint8_t req[2] = {(uint8_t)(want & 0xFF), (uint8_t)((want >> 8) & 0xFF)};
        if (!Rp2040SdTransport::request(RP2040_PKT_SD_READ, req, 2)) break;
        if (Rp2040SdTransport::resp_cmd != RP2040_PKT_SD_RESPONSE || Rp2040SdTransport::resp_len < 3) break;
        const uint8_t* r = Rp2040SdTransport::resp_buf;
        uint16_t got = (uint16_t)r[1] | ((uint16_t)r[2] << 8);
        if (got == 0) break;  // EOF
        size_t available_in_resp = Rp2040SdTransport::resp_len - 3;
        if (got > available_in_resp) got = available_in_resp;  // defensive
        memcpy(buf + total, r + 3, got);
        total += got;
        if (got < want) break;  // short read -> EOF
      }
      return total;
    }

    void flush() override {
      // No explicit flush command -- the RP2040 firmware already flushes
      // after every write (see handleSdWrite() in rp2040_bridge.ino).
    }

    bool seek(uint32_t pos, SeekMode mode) override {
      // Not supported by the bridge protocol. Nothing in this codebase
      // calls File::seek() (confirmed by searching every call site before
      // building this), so failing cleanly here is safe.
      (void)pos;
      (void)mode;
      return false;
    }

    size_t position() const override {
      return 0;  // Not tracked; matches seek()'s "unsupported" stance.
    }

    size_t size() const override {
      if (_kind == Kind::StatOnly) return _statSize;
      if (!staleCheck()) return 0;
      if (!Rp2040SdTransport::request(RP2040_PKT_SD_SIZE, nullptr, 0)) return 0;
      if (Rp2040SdTransport::resp_cmd != RP2040_PKT_SD_RESPONSE || Rp2040SdTransport::resp_len < 5) return 0;
      const uint8_t* r = Rp2040SdTransport::resp_buf;
      return (uint32_t)r[1] | ((uint32_t)r[2] << 8) | ((uint32_t)r[3] << 16) | ((uint32_t)r[4] << 24);
    }

    bool setBufferSize(size_t size) override {
      (void)size;
      return true;  // No local buffering to size; accept and no-op.
    }

    void close() override {
      if (_kind == Kind::RealFile) {
        if (_generation == g_open_generation) {
          Rp2040SdTransport::request(RP2040_PKT_SD_CLOSE, nullptr, 0);
          g_open_generation++;  // nothing is open on the RP2040 now
        }
      }
      _kind = Kind::Closed;
    }

    time_t getLastWrite() override {
      return 0;  // Not tracked by the protocol.
    }

    const char* path() const override {
      return _path.c_str();
    }

    const char* name() const override {
      if (_kind == Kind::StatOnly) return _name.c_str();
      int slash = _path.lastIndexOf('/');
      return (slash >= 0) ? (_path.c_str() + slash + 1) : _path.c_str();
    }

    boolean isDirectory(void) override {
      if (_kind == Kind::StatOnly) return _statIsDir;
      return _kind == Kind::Directory;
    }

    fs::FileImplPtr openNextFile(const char* mode) override {
      (void)mode;
      if (_kind != Kind::Directory || _dirIndex >= (size_t)_entries.size()) return fs::FileImplPtr();
      Rp2040SdTransport::DirEntry e = _entries.get(_dirIndex++);
      String child_path = (_path == "/") ? ("/" + e.name) : (_path + "/" + e.name);
      return std::make_shared<Rp2040SdFileImpl>(child_path, e.name, e.size, e.is_dir);
    }

    boolean seekDir(long position) override {
      if (_kind != Kind::Directory || position < 0 || (size_t)position > (size_t)_entries.size()) return false;
      _dirIndex = (size_t)position;
      return true;
    }

    String getNextFileName(void) override {
      bool unused;
      return getNextFileName(&unused);
    }

    String getNextFileName(bool* isDir) override {
      if (_kind != Kind::Directory || _dirIndex >= (size_t)_entries.size()) return String();
      Rp2040SdTransport::DirEntry e = _entries.get(_dirIndex++);
      if (isDir) *isDir = e.is_dir;
      return e.name;
    }

    void rewindDirectory(void) override {
      _dirIndex = 0;
    }

    operator bool() override {
      return _kind != Kind::Closed;
    }

  private:
    bool staleCheck() const {
      return _kind == Kind::RealFile && _generation == g_open_generation;
    }

    String _path;
    String _name;  // Kind::StatOnly only
    Kind _kind = Kind::Closed;
    uint32_t _generation = 0;

    // Kind::Directory
    LinkedList<Rp2040SdTransport::DirEntry> _entries;
    size_t _dirIndex = 0;

    // Kind::StatOnly
    uint32_t _statSize = 0;
    bool _statIsDir = false;

  public:
    // Shared across every Rp2040SdFileImpl instance: bumped on every
    // successful open and every close. See the class-level comment
    // (constraint 1) for why this exists.
    static inline uint32_t g_open_generation = 0;
};

// ============================================================
// FSImpl
// ============================================================
class Rp2040SdFSImpl : public fs::FSImpl {
  public:
    fs::FileImplPtr open(const char* path, const char* mode, const bool create) override {
      (void)create;  // The bridge's WRITE mode already implies create+truncate.
      String p = normalizePath(path);

      uint8_t sd_mode = 0x00;  // SD_MODE_READ
      if (mode) {
        if (mode[0] == 'w') sd_mode = 0x01;       // SD_MODE_WRITE
        else if (mode[0] == 'a') sd_mode = 0x02;  // SD_MODE_APPEND
      }

      size_t path_len = p.length();
      if (path_len > 126) path_len = 126;  // matches rp2040_bridge.ino's 128-byte buffer minus NUL
      uint8_t payload[1 + 126];
      payload[0] = sd_mode;
      memcpy(&payload[1], p.c_str(), path_len);

      if (!Rp2040SdTransport::request(RP2040_PKT_SD_OPEN, payload, 1 + path_len) ||
          Rp2040SdTransport::resp_cmd != RP2040_PKT_SD_RESPONSE || Rp2040SdTransport::resp_len < 2) {
        return std::make_shared<Rp2040SdFileImpl>(p, false, false, 0);
      }

      bool is_dir = Rp2040SdTransport::resp_buf[1] != 0;
      Rp2040SdFileImpl::g_open_generation++;
      return std::make_shared<Rp2040SdFileImpl>(p, true, is_dir, Rp2040SdFileImpl::g_open_generation);
    }

    bool exists(const char* path) override {
      String p = normalizePath(path);
      if (!Rp2040SdTransport::request(RP2040_PKT_SD_EXISTS, (const uint8_t*)p.c_str(), p.length())) return false;
      if (Rp2040SdTransport::resp_cmd != RP2040_PKT_SD_RESPONSE || Rp2040SdTransport::resp_len < 2) return false;
      return Rp2040SdTransport::resp_buf[1] != 0;
    }

    bool rename(const char* pathFrom, const char* pathTo) override {
      String from = normalizePath(pathFrom);
      String to = normalizePath(pathTo);
      // fromLen is a 1-byte length prefix (see PKT_SD_RENAME's payload
      // format in rp2040_bridge.ino) -- both paths also share one 512-byte
      // packet, well under which 255 each comfortably fits.
      if (from.length() > 255 || to.length() > 255 || (size_t)(1 + from.length() + to.length()) > RP2040_MAX_PACKET - 1) {
        return false;
      }
      uint8_t payload[RP2040_MAX_PACKET - 1];
      payload[0] = (uint8_t)from.length();
      memcpy(&payload[1], from.c_str(), from.length());
      memcpy(&payload[1 + from.length()], to.c_str(), to.length());
      if (!Rp2040SdTransport::request(RP2040_PKT_SD_RENAME, payload, 1 + from.length() + to.length())) return false;
      return Rp2040SdTransport::resp_cmd == RP2040_PKT_SD_RESPONSE;
    }

    bool remove(const char* path) override {
      String p = normalizePath(path);
      if (!Rp2040SdTransport::request(RP2040_PKT_SD_REMOVE, (const uint8_t*)p.c_str(), p.length())) return false;
      return Rp2040SdTransport::resp_cmd == RP2040_PKT_SD_RESPONSE;
    }

    bool mkdir(const char* path) override {
      String p = normalizePath(path);
      if (!Rp2040SdTransport::request(RP2040_PKT_SD_MKDIR, (const uint8_t*)p.c_str(), p.length())) return false;
      return Rp2040SdTransport::resp_cmd == RP2040_PKT_SD_RESPONSE;
    }

    bool rmdir(const char* path) override {
      String p = normalizePath(path);
      if (!Rp2040SdTransport::request(RP2040_PKT_SD_RMDIR, (const uint8_t*)p.c_str(), p.length())) return false;
      return Rp2040SdTransport::resp_cmd == RP2040_PKT_SD_RESPONSE;
    }

  private:
    static String normalizePath(const char* path) {
      if (path == nullptr || path[0] == '\0') return String("/");
      return String(path);
    }
};

// Global `SD`, used exactly like the real SD library's `SD` global
// everywhere else in this codebase -- but see the class-level comment:
// card type/size aren't exposed by the bridge protocol, so
// SDInterface::initSD() has a MARAUDER_SENSECAP-specific branch that
// doesn't call SD.cardType()/SD.cardSize() (those aren't part of the
// generic fs::FS interface this provides, only the real SDFS subclass).
inline fs::FS SD(std::make_shared<Rp2040SdFSImpl>());

#endif  // MARAUDER_SENSECAP
#endif
