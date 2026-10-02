# SenseCAP Indicator — Marauder Test Plan (Part 1: Boot, Touch, Device, Recon, GPS)

Branch under test: `sensecap-indicator-latest`.
Mark each step `[x]` pass, `[F]` fail, `[~]` partial, with notes.

**(HW-UNKNOWN)** marks a spot where this port's wiring/behavior has not been
verified by anyone — these are the most likely bug sites, so record exactly
what you observe, not just pass/fail.

## 0. Setup

- Serial monitor at 115200 baud, newline `\n`.
- [ ] 0.1 Record: arduino-esp32 core version ____, NimBLE version ____.
- [ ] 0.2 Reset device, watch serial. Expected: boot log, no reboot loop, no crash backtrace.
- [ ] 0.3 Type `help` in serial. Expected: command list prints.

## 1. Boot, display, touch

- [ ] 1.1 Power on. Expected: backlight on, boot banner readable, no tearing/garbled image.
- [ ] 1.2 Main menu appears within ~5s automatically.
- [ ] 1.3 Main menu items top-to-bottom: Recon, WiFi, Bluetooth, GPS, Device, Reboot.
- [ ] 1.4 Colors look correct (no red/blue channel swap).
- [ ] 1.5 Tap each main menu button individually. Expected: the tapped button activates — not a neighboring one. Touch mapping is `479-x, 479-y`; note any mirrored/rotated behavior. **(HW-UNKNOWN)**
- [ ] 1.6 Tap near screen edges/corners. Expected: still registers correctly, no dead zones.
- [ ] 1.7 Rapid taps and press-and-hold. Expected: no double-activation, no freeze.
- [ ] 1.8 Open a menu with >14 items (WiFi > Sniffers). Expected: pages correctly, every item reachable.
- [ ] 1.9 Find a long menu label. Expected: it scrolls/marquees rather than being cut off or overflowing.
- [ ] 1.10 Status bar at top updates (free RAM, mode, SD/GPS indicators) roughly every second.
- [ ] 1.11 Navigate into/out of 5 submenus. Expected: no redraw artifacts left over.
- [ ] 1.12 Idle 10 minutes. Expected: stable, no flicker, no reboot.

## 2. Navigation

- [ ] 2.1 Main > WiFi > Back returns to Main in one tap.
- [ ] 2.2 Descend 4 levels and return via repeated Back; each Back goes up exactly one level.
- [ ] 2.3 Main > Reboot restarts the device and it comes back to the main menu.

## 3. Device menu

Main > Device. Expected items: Back, Brightness, Device Info, Settings, Geofences, plus SD-dependent items only if SD is detected.

- [ ] 3.1 Brightness: tap-top screen appears with a bar; tapping top/bottom changes the bar; tapping middle or waiting 3s saves and exits.
      **(HW-UNKNOWN)**: SenseCAP's backlight is switched via `digitalWrite` (on/off only), not PWM — the bar may move on screen without the backlight actually dimming. Record what you actually observe.
- [ ] 3.2 Serial: `brightness` prints the current level.
- [ ] 3.3 Serial: `brightness -c` cycles it, `brightness -s 3` sets it; record whether backlight visibly changes.
- [ ] 3.4 Device Info shows: firmware version, hardware name "Marauder SenseCAP", free heap, MAC address, SD status.
- [ ] 3.5 Settings menu lists boolean toggles (ForcePMKID, ForceProbe, SavePCAP, EnableLED, EPDeauth, ChanHop, others). Tapping one flips its shown state.
- [ ] 3.6 Toggle a few settings, reboot, reopen Settings — values should persist.
- [ ] 3.7 Serial: `settings` prints current JSON; `settings -s SavePCAP enable` then `settings` shows the change; `settings -r` resets to defaults.
- [ ] 3.8 EnableLED toggle: no addressable LED exists on this board, so expect no visible effect and no crash — just confirm it doesn't error.

## 4. Recon (new upstream dashboard feature)

Main > Recon > Start WiFi / Start BLE.

- [ ] 4.1 Start WiFi: dashboard shows device counts, a churn graph, and a signal-scale graph with axis labels on the right of the plot, plus a small status indicator top-left. Counts rise as nearby devices are heard.
- [ ] 4.2 Let it run 2 minutes near a WiFi AP and a phone connected to it (normal use, not an attack). Expected: the AP and the phone (as a station) both appear once the phone generates some traffic.
- [ ] 4.3 Check axis labels are right-aligned and not overlapping the plot or running off-screen (this uses the newly-added `drawRightString`/`textWidth` shim methods).
- [ ] 4.4 Tap to exit. Expected: returns to Recon menu, scan stops.
- [ ] 4.5 Start BLE: dashboard counts nearby BLE devices; toggling a phone's Bluetooth on/off should change the count.
- [ ] 4.6 Serial: `recon wifi`, `recon ble`, `recon status`, `recon stop` behave as named.
- [ ] 4.7 **(HW-UNKNOWN, needs SD)**: if SD works, check a recon mission folder with a manifest + observation files gets created and the manifest is marked complete on clean stop.
- [ ] 4.8 Run each Recon mode 10 minutes continuously. Expected: no reboot, free RAM doesn't steadily drain toward 0.

## 11. GPS **(HW-UNKNOWN — needs a GPS module wired up)**

### 11.0 Wiring
This build's `configs.h` sets: UART1, **GPS_TX = GPIO14, GPS_RX = GPIO13**, 9600 baud default.
Wire GPS module TX -> ESP32 GPIO13, GPS module RX -> ESP32 GPIO14, plus GND and power.

Before testing, confirm GPIO13/14 are actually free/exposed for this purpose on your
specific Indicator unit — some variants use those pins for other onboard peripherals.
If your module is wired to different pins, tell me and I'll update `configs.h` to match;
a pin mismatch is the single most likely cause of "no GPS data at all."

- [ ] 11.0a Power up with GPS module connected and antenna with clear sky view. First fix may take 1-10 minutes cold.

### 11.1 Main > GPS menu

Items: Back, GPS Data, NMEA Stream, GPS Tracker, GPS POI.

- [ ] 11.1 GPS Data with no fix yet: shows fix=No, a satellite count (may be nonzero before fix), lat/lon blank or 0.
- [ ] 11.2 GPS Data with a fix: fix=Yes, satellites >= 4, latitude/longitude roughly matching your real location (sanity-check against a phone map app), plausible altitude, correct UTC date/time, an accuracy/HDOP figure. Updates about once per second.
- [ ] 11.3 Physically move ~50+ meters. Expected: lat/lon update to match.
- [ ] 11.4 NMEA Stream: raw NMEA sentences scroll on screen and on serial (lines starting `$GP...`/`$GN...`), with valid-looking checksums.
- [ ] 11.5 GPS Tracker: starts logging; screen/status indicates active tracking; point count increases over time. Persisting the track needs SD. **(HW-UNKNOWN)**
- [ ] 11.6 GPS POI > Mark POI: with a fix, shows "POI Logged"; without a fix (or without working SD), shows "POI Log Failed" — that failure message *is* the expected/correct behavior in that case, not a bug.
- [ ] 11.7 Back out of GPS Data/NMEA/Tracker screens: the scan stops cleanly (status bar returns to idle) except GPS Tracker, which is designed to keep running in the background after Back.

### 11.2 WiFi > Wardriving (requires GPS fix)

- [ ] 11.8 Confirm "Wardriving" appears under the WiFi menu only when GPS is enabled in this build.
- [ ] 11.9 Start a beacon wardrive with a GPS fix present and walk/drive around. Expected: AP count increases with location tagged; a log is written if SD works.
- [ ] 11.10 Start wardriving with no GPS fix. Expected: a clear message that it's waiting for a fix, not a crash or silent no-op.
- [ ] 11.11 Serial: `gps`, `gpsdata`, `nmea`, `gpstracker -c start`, `gpstracker -c stop` behave as the `help` text for each describes.
- [ ] 11.12 Serial: `wardrivepoi <label>` tags a POI with that label during an active wardrive.
- [ ] 11.13 Geofences (Device > Geofences): set one slot to a radius around your current GPS fix, then physically move outside/inside that radius. Expected: the device's status/behavior reflects being inside vs. outside the geofence (check the Geofences screen and/or status bar for the indicator).

---
Part 2 (WiFi feature menus, scanners) and Part 3 (CLI reference, memory soak) are separate files: `SENSECAP_TEST_PLAN_part2.md`, `SENSECAP_TEST_PLAN_part3.md`.
