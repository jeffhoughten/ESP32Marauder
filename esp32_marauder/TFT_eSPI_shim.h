#pragma once
// TFT_eSPI_Shim.h
// Drop-in replacement for TFT_eSPI.h for the SenseCAP Indicator Marauder port.
// Routes TFT_eSPI API calls to Arduino_GFX + LVGL.

#ifndef TFT_ESPI_SHIM_H
#define TFT_ESPI_SHIM_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
//#include <Adafruit_GFX.h>
#include <Fonts/FreeMono9pt7b.h>

// =====================
// RGB565 Color Constants
// =====================
#define TFT_BLACK       RGB565_BLACK
#define TFT_NAVY        RGB565_NAVY
#define TFT_DARKGREEN   RGB565_DARKGREEN
#define TFT_DARKCYAN    RGB565_DARKCYAN
#define TFT_MAROON      RGB565_MAROON
#define TFT_PURPLE      RGB565_PURPLE
#define TFT_OLIVE       RGB565_OLIVE
#define TFT_LIGHTGREY   RGB565_LIGHTGREY
#define TFT_DARKGREY    RGB565_DARKGREY
//#define TFT_BLUE        0x001F
#define TFT_BLUE        RGB565(0, 0, 255)
#define TFT_GREEN       RGB565_GREEN
#define TFT_CYAN        RGB565_CYAN
#define TFT_RED         RGB565_RED
#define TFT_MAGENTA     RGB565_MAGENTA
#define TFT_YELLOW      RGB565_YELLOW
#define TFT_WHITE       RGB565_WHITE
#define TFT_ORANGE      RGB565_ORANGE
#define TFT_GREENYELLOW RGB565_GREENYELLOW
#define TFT_PINK        RGB565_PINK
#define TFT_BROWN       RGB565_BROWN
#define TFT_GOLD        RGB565_GOLD
#define TFT_SILVER      RGB565_SILVER
#define TFT_SKYBLUE     RGB565_SKYBLUE
#define TFT_VIOLET      RGB565_VIOLET
#define TFT_GRAY        RGB565_GRAY
#define TFT_GREY        RGB565_GREY
#define TFT_FARTGRAY    0x4208

// Text datum constants (mirroring TFT_eSPI)
#define TL_DATUM        0
#define TC_DATUM        1
#define TR_DATUM        2
#define ML_DATUM        3
#define MC_DATUM        4
#define MR_DATUM        5
#define BL_DATUM        6
#define BC_DATUM        7
#define BR_DATUM        8

#define TFT_CS    -1
#define TFT_DC    -1
#define TFT_RST   -1
#define TFT_MISO  -1
#define TFT_MOSI  -1
#define TFT_SCLK  -1

// Font placeholder — Marauder uses GFX fonts via setFreeFont()
// We store a pointer but don't actually use it for rendering
// struct GFXfont;

// Forward declaration
extern Arduino_RGB_Display *gfx;

class TFT_eSPI_Button {
public:
  int16_t _x1, _y1;  // top-left corner
  uint16_t _w, _h;
  uint16_t _outlineColor, _fillColor, _textColor;
  char _label[30];
  uint8_t _textsize;
  int16_t _labelOffsetX, _labelOffsetY;
  uint8_t _labelDatum;
  bool _currstate, _laststate;

  TFT_eSPI_Button() : _currstate(false), _laststate(false), 
    _labelOffsetX(0), _labelOffsetY(0), _labelDatum(ML_DATUM) {}

  void initButton(void* tft, int16_t x, int16_t y, uint16_t w, uint16_t h,
                  uint16_t outline, uint16_t fill, uint16_t textcolor, 
                  char* label, uint8_t textsize) {
    _x1 = x - w/2;
    _y1 = y - h/2;
    _w = w;
    _h = h;
    _outlineColor = outline;
    _fillColor = fill;
    _textColor = textcolor;
    _textsize = textsize;
    strncpy(_label, label, 29);
    _label[29] = 0;
  }

  void drawButton(bool inverted = false, String long_name = "") {
    if (!gfx) return;
    String label = long_name.length() > 0 ? long_name : String(_label);
    uint16_t fill = inverted ? _textColor : _fillColor;
    uint16_t text = inverted ? _fillColor : _textColor;
    gfx->fillRect(_x1, _y1, _w, _h, fill);
    if (_outlineColor != _fillColor)
      gfx->drawRect(_x1, _y1, _w, _h, _outlineColor);
    gfx->setTextColor(text, fill);
    gfx->setTextSize(_textsize);
    gfx->setFont(NULL);
    gfx->setCursor(_x1 + 26, _y1 + _h/2 - 4);
    gfx->print(label);
  }

  void setLabelDatum(int16_t x, int16_t y, uint8_t datum = MC_DATUM) {
    _labelOffsetX = x;
    _labelOffsetY = y;
    _labelDatum = datum;
  }

  bool contains(int16_t x, int16_t y) {
    return (x >= _x1 && x < (_x1+_w) && y >= _y1 && y < (_y1+_h));
  }

  void press(bool p) { _laststate = _currstate; _currstate = p; }
  bool isPressed()   { return _currstate; }
  bool justPressed()  { return (_currstate && !_laststate); }
  bool justReleased() { return (!_currstate && _laststate); }
};

// =====================
// TFT_eSPI shim class
// =====================
class TFT_eSPI {
public:
  // Cursor tracking
  int16_t _cursor_x = 0;
  int16_t _cursor_y = 0;

  // Text state
  uint16_t _text_color = TFT_WHITE;
  uint16_t _text_bg_color = TFT_BLACK;
  uint8_t  _text_size = 1;
  uint8_t  _text_datum = TL_DATUM;
  bool     _text_wrap = true;
  bool     _text_wrap_y = true;
  const GFXfont* _free_font = nullptr;

  // Rotation
  uint8_t _rotation = 0;

  // Button array support
  TFT_eSPI_Button _buttons[15];

  // ---- Initialization ----
  TFT_eSPI() {}

  void init() {
    // Display is already initialized in setup()
    // Nothing to do here
  }

  void begin() { init(); }

  // ---- Rotation ----
  void setRotation(uint8_t r) {
    _rotation = r;
    // Rotation handled at LVGL level
  }

  uint8_t getRotation() {
    return _rotation;
  }

  // ---- Screen fill ----
  void fillScreen(uint16_t color) {
    gfx->fillScreen(color);
  }

  // ---- Rectangle drawing ----
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    gfx->fillRect(x, y, w, h, color);
  }

  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    gfx->drawRect(x, y, w, h, color);
  }

  // ---- Line drawing ----
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) {
    gfx->drawLine(x0, y0, x1, y1, color);
  }

  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) {
    gfx->drawFastHLine(x, y, w, color);
  }

  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color) {
    gfx->drawFastVLine(x, y, h, color);
  }

  // ---- Bitmap drawing ----
  void drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                  int16_t w, int16_t h, uint16_t color) {
    gfx->drawXBitmap(x, y, bitmap, w, h, color);
  }

  void drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                  int16_t w, int16_t h, uint16_t color, uint16_t bg) {
    gfx->fillRect(x, y, w, h, bg);
    gfx->drawXBitmap(x, y, bitmap, w, h, color);
  }

  // ---- Cursor ----
  void setCursor(int16_t x, int16_t y) {
    _cursor_x = x;
    _cursor_y = y;
    gfx->setCursor(x, y);
  }

  int16_t getCursorY() {
    return _cursor_y;
  }

  int16_t getCursorX() {
    return _cursor_x;
  }

  // ---- Text styling ----
  void setTextColor(uint16_t color) {
    _text_color = color;
    _text_bg_color = TFT_BLACK;
    gfx->setTextColor(color);
  }

  void setTextColor(uint16_t color, uint16_t bg) {
    _text_color = color;
    _text_bg_color = bg;
    gfx->setTextColor(color, bg);
  }

  void setTextColor(uint16_t color, uint16_t bg, bool bgfill) {
    _text_color = color;
    _text_bg_color = bg;
    gfx->setTextColor(color, bg);
  }

  void setTextSize(uint8_t size) {
    _text_size = size;
    gfx->setTextSize(size);
  }

  void setTextWrap(bool wrap) {
    _text_wrap = wrap;
    gfx->setTextWrap(wrap);
  }

  void setTextWrap(bool wrapX, bool wrapY) {
    _text_wrap = wrapX;
    _text_wrap_y = wrapY;
    gfx->setTextWrap(wrapX);
  }

  void setTextDatum(uint8_t datum) {
    _text_datum = datum;
  }

  void setFreeFont(const GFXfont* font) {
    _free_font = font;
    // Arduino_GFX uses its own font system
    // We keep this as a no-op for now; text will render with default font
  }

  // ---- Text output ----
  void print(const String& s) {
    gfx->print(s);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void print(const char* s) {
    gfx->print(s);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void print(int n, int base = DEC) {
    gfx->print(n, base);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void print(float n, int digits = 2) {
    gfx->print(n, digits);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void println(const String& s) {
    gfx->println(s);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void println(const char* s) {
    gfx->println(s);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void println(int n, int base = DEC) {
    gfx->println(n, base);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void println() {
    gfx->println();
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  // ---- String drawing with position ----
  void drawString(const String& string, int32_t x, int32_t y, uint8_t font = 1) {
    gfx->setCursor(x, y);
    gfx->setTextColor(_text_color, _text_bg_color);
    gfx->print(string);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void drawString(const char* string, int32_t x, int32_t y, uint8_t font = 1) {
    gfx->setCursor(x, y);
    gfx->setTextColor(_text_color, _text_bg_color);
    gfx->print(string);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void drawCentreString(const String& string, int32_t x, int32_t y, uint8_t font = 1) {
    // Approximate center by offsetting by half the string pixel width
    // Using 6 pixels per char as a rough estimate for default font
    int16_t offset = (string.length() * 6 * _text_size) / 2;
    gfx->setCursor(x - offset, y);
    gfx->setTextColor(_text_color, _text_bg_color);
    gfx->print(string);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void drawCentreString(const char* string, int32_t x, int32_t y, uint8_t font = 1) {
    drawCentreString(String(string), x, y, font);
  }

  // Pixel width of a string in the current font/size
  int16_t textWidth(const String& string, uint8_t font = 1) {
    int16_t x1, y1;
    uint16_t w, h;
    gfx->setTextSize(_text_size);
    gfx->getTextBounds(string.c_str(), 0, 0, &x1, &y1, &w, &h);
    return w;
  }

  int16_t textWidth(const char* string, uint8_t font = 1) {
    return textWidth(String(string), font);
  }

  void drawRightString(const String& string, int32_t x, int32_t y, uint8_t font = 1) {
    gfx->setCursor(x - textWidth(string, font), y);
    gfx->setTextColor(_text_color, _text_bg_color);
    gfx->print(string);
    _cursor_x = gfx->getCursorX();
    _cursor_y = gfx->getCursorY();
  }

  void drawRightString(const char* string, int32_t x, int32_t y, uint8_t font = 1) {
    drawRightString(String(string), x, y, font);
  }

  // Bitmap font selection is not applicable on the Arduino_GFX backend
  void setTextFont(uint8_t font) {}

  void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t color) {
    gfx->fillCircle(x, y, r, color);
  }

  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
    gfx->drawRoundRect(x, y, w, h, r, color);
  }

  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
    gfx->fillRoundRect(x, y, w, h, r, color);
  }

  // ---- Touch (replaced by SenseCAP touch stack) ----
  // These are stubs — actual touch is handled via touch.h
  uint8_t getTouch(uint16_t *x, uint16_t *y, uint16_t threshold = 600) {
    return 0; // Handled externally
  }

  void setTouch(uint16_t *data) {
    // No-op — SenseCAP uses capacitive touch, no calibration needed
  }

  // ---- Low level stubs ----
  void writecommand(uint8_t cmd) {}
  void writedata(uint8_t data) {}

  // ---- Display dimensions ----
  int16_t width()  { return 480; }
  int16_t height() { return 480; }
};

#endif // TFT_ESPI_SHIM_H
