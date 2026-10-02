# SenseCAP Indicator — Marauder Test Plan (Part 2: WiFi, Bluetooth, SD)

Continuation of `SENSECAP_TEST_PLAN.md`. Same marking convention.

> **Scope note:** this device's WiFi/BLE features generate real over-the-air frames.
> Run anything that transmits (beacon lists, deauth, probe floods, BLE advertising,
> evil portal) only against access points, phones, and other equipment that you own
> or have explicit authorization to test, consistent with Marauder's own intended use
> as a security-research/pentest tool. Passive-only items (sniffers, scanners that just
> listen, GPS, recon) have no such restriction beyond normal RF courtesy.

## 5. WiFi menu

Main > WiFi: Back, Sniffers, Scanners, Wardriving (GPS-gated, see Part 1 section 11.2), Attacks, General.

### 5A. Sniffers (passive — listen only)

For each: tap to start, watch live output, tap screen to stop, confirm it returns to the menu cleanly. With `SavePCAP` enabled and SD working, each should write a `.pcap` you can open in Wireshark.

- [ ] 5A.1 Probe Request Sniff: toggle a phone's WiFi off/on nearby — probe lines with SSID/MAC/RSSI appear.
- [ ] 5A.2 Beacon Sniff: nearby APs including yours show SSID/BSSID/channel/RSSI.
- [ ] 5A.3 Deauth Sniff: shows any deauth frames in the air with src/dst/channel/RSSI (channel field is new upstream).
- [ ] 5A.4 Packet Count: counters update continuously.
- [ ] 5A.5 EAPOL/PMKID Scan: reconnect a test phone to your test AP — handshake count increases.
- [ ] 5A.6 Packet Monitor: bar graph of packet rate per channel; on-screen channel +/- and hop-toggle buttons respond to touch. **(HW-UNKNOWN: graph geometry on 480x480 screen)**
- [ ] 5A.7 Channel Analyzer: per-channel activity/signal graph (2.4GHz 1-13, plus 5GHz if dual-band).
- [ ] 5A.8 Channel Summary: table of channel utilization.
- [ ] 5A.9 Raw Capture: raw frame counter increases; with SavePCAP+SD, file grows.
- [ ] 5A.10 Detect Pwnagotchi / Detect Pineapple / Detect MultiSSID: each runs without error; will report only if such a device is actually nearby, otherwise stays quiet.
- [ ] 5A.11 Scan AP/STA: run ~30s, stop — AP and station lists populate (needed by Select menus in 5E).
- [ ] 5A.12 Fox Hunt: pick a target from APs/Stations/Pineapples/MultiSSID lists. A live RSSI tracker should change value as you move the device closer/farther from that target. Test the Sort menu (Signal strongest / Name-MAC A-Z / Channel low-high) and Filter menu (All / Seen in 30s / 2.4GHz / 5GHz).
- [ ] 5A.13 MAC Monitor: tracks whether a chosen MAC is currently seen.
- [ ] 5A.14 SAE Commit: reports WPA3 SAE commit frames if a WPA3 test network is active.

### 5B. Scanners (active probing of hosts — use your own network/LAN)

Needs the device joined to a WiFi network first (5E "Join WiFi").

- [ ] 5B.1 Ping Scan: sweeps the subnet, lists responding IPs.
- [ ] 5B.2 ARP Scan: lists IP + MAC pairs on the LAN.
- [ ] 5B.3 Port Scan All: pick an IP from "Active IPs" — reports open ports on that host.
- [ ] 5B.4 SSH/Telnet/SMTP/DNS/HTTP/HTTPS/RDP Scan: each checks that specific service across the subnet; point it at a host you know is running that service to confirm detection.
- [ ] 5B.5 Stop mid-scan by tapping — stops promptly, returns to menu.

### 5C. Attacks ⚠ test against equipment you own/control only

Beacon/SSID attacks need SSIDs added first (5E). Targeted attacks need APs/Stations selected first (5E).

- [ ] 5C.1 Beacon Spam List: your custom SSIDs (added in 5E) appear in a phone's WiFi scan list.
- [ ] 5C.2 Beacon Spam Random: many randomly-named SSIDs appear in a phone's WiFi scan list.
- [ ] 5C.3 Funny SSID Beacon / Rick Roll Beacon: preset joke/lyric SSIDs appear in a phone's WiFi scan list.
- [ ] 5C.4 Probe Req Flood: with APs selected, probe frames are transmitted (confirm with a second device running a sniffer, or via packet count on this device).
- [ ] 5C.5 Deauth Flood (your test AP only): clients connected to your test AP disconnect.
- [ ] 5C.6 Deauth Targeted: with an AP and a specific station selected, only that station disconnects, others on the AP stay connected.
- [ ] 5C.7 AP Clone Spam: clones of a selected AP's SSID show up in a WiFi scan list.
- [ ] 5C.8 Karma: using a probe-SSID list collected earlier, confirm it answers probe requests for a chosen SSID.
- [ ] 5C.9 Bad Msg / Bad Msg Targeted, Assoc Sleep / Assoc Sleep Targ, SAE Commit Flood, Channel Switch, Quiet Time: each is a distinct malformed/control frame type aimed at a selected AP (and station where applicable); confirm each starts, transmits (visible via a sniffer on a second device), and stops cleanly without crashing this device.
- [ ] 5C.10 Stop each attack by tapping Exit: TX ceases, status bar returns to idle.

### 5D. Evil Portal (captive-portal test page — your own test AP name/SSID)

- [ ] 5D.1 WiFi > General > Select EP HTML File lists `.html` files from SD (if SD works). **(HW-UNKNOWN)**
- [ ] 5D.2 Evil Portal > AP Config: set an AP name via the on-screen keyboard; text entry works correctly.
- [ ] 5D.3 Start Evil Portal: broadcasts an open test AP with that name; a phone can join it and gets redirected to a captive-portal page.
- [ ] 5D.4 Submit the test form on the phone: submitted field values show on the device screen/serial, and are saved to an SD log if SD works.
- [ ] 5D.5 Serial: `evilportal -c start -w html.html` does the same as the touch flow.
- [ ] 5D.6 With `EPDeauth` setting on, clients on the real test AP get disconnected while the portal runs (confirms the deauth-assist behavior).

### 5E. General (WiFi > General)

- [ ] 5E.1 Generate SSIDs: creates N random SSIDs; count shown updates.
- [ ] 5E.2 Add SSIDs: on-screen keyboard appears; type a test SSID like "TestSSID1"; every key registers, shift/symbols/backspace all work, Enter adds it to the list. **(HW-UNKNOWN: keyboard key hit-geometry on this screen size)**
- [ ] 5E.3 Select probe SSIDs: lists captured probe SSIDs; tap to select/deselect.
- [ ] 5E.4 Clear SSIDs / Clear APs / Clear Stations: confirms, then the relevant list is empty (check via the Select menus).
- [ ] 5E.5 Select APs: "Select ALL" plus individual toggles; selected entries are visibly marked.
- [ ] 5E.6 View AP Info: shows SSID, BSSID, channel, RSSI, security type, WPS status, vendor.
- [ ] 5E.7 Select Stations: pick an AP, then its known stations.
- [ ] 5E.8 Join WiFi: pick an AP, type a password on the keyboard, watch a "Trying n/N" status, then "Connected" with an IP shown. Try a wrong password too — expect a clear failure message, not a crash.
- [ ] 5E.9 Join Saved WiFi / Manage Saved WiFi: save up to 5 profiles, join one by name, remove one; list survives a reboot.
- [ ] 5E.10 Start AP / Host AP Info: starts a SoftAP with a chosen name; a phone can see and join it; info screen shows IP and connected client count.
- [ ] 5E.11 Set MACs: Generate AP MAC, Generate STA MAC, Clone AP MAC, Clone STA MAC — each changes the device's MAC address; confirm via Device Info or a sniffer on another device.
- [ ] 5E.12 Shutdown WiFi: turns radio off cleanly; status bar reflects it; device remains otherwise responsive.
- [ ] 5E.13 Upload Wardrive Logs (needs SD + WiFi + API keys configured in Settings `wu`/`wt`): Destination menu WiGLE/WDGWars/Both; shows per-file upload progress and success/failure. Without credentials set, expect a clear error, not a crash. **(HW-UNKNOWN)**

## 6. Bluetooth menu

Main > Bluetooth: Back, Sniffers, Bluetooth Attacks.

### 6A. Sniffers (passive)

- [ ] 6A.1 Bluetooth Sniffer: lists nearby BLE advertisers (MAC/RSSI/name); toggling a phone's Bluetooth on/off changes what's seen.
- [ ] 6A.2 Flipper Sniff: detects Flipper Zero BLE if one is present; otherwise stays empty — confirm it doesn't error either way.
- [ ] 6A.3 FindMy Sniff / FindMy Monitor: lists Apple FindMy/AirTag advertisements; shows entries if an AirTag or similar tag is nearby.
- [ ] 6A.4 Detect Card Skimmers: flags known skimmer BLE device-name patterns; silent if none nearby.
- [ ] 6A.5 Bluetooth Analyzer: live summary/graph of BLE traffic.
- [ ] 6A.6 Flock Sniff / Meta Detect: detect their respective BLE signatures; silent if none nearby.
- [ ] 6A.7 Fox Hunt (BLE): pick a target from BLE Devices/FindMy/Flipper/Meta/Flock lists; RSSI tracker updates as you move closer/farther.
- [ ] 6A.8 Stop each cleanly. Watch specifically for NimBLE-related crashes at start/stop — the port's commit message flagged a NimBLE 2.3.7 init crash that needed 2.3.8+, so this is a known-fragile area.

### 6B. Bluetooth Attacks ⚠ test with your own phone(s)/devices; these cause visible pairing notifications

Each item below advertises BLE packets that trigger a specific phone/OS's device-discovery popup. Confirm with your own test phone that the expected OS shows its notification, then stop the test and confirm advertising ceases (re-run the BLE sniffer from 6A to verify).

- [ ] 6B.1 Sour Apple: iOS-style pairing popup on a test iPhone.
- [ ] 6B.2 Apple Juice: Apple-accessory-style popup on a test iPhone.
- [ ] 6B.3 Swiftpair Spam: "new device found" notification on a test Windows PC.
- [ ] 6B.4 Samsung BLE Spam: pairing popup on a test Samsung device.
- [ ] 6B.5 Google BLE Spam: Fast Pair popup on a test Android device.
- [ ] 6B.6 Flipper BLE Spam: appears as Flipper-named BLE devices in a scanner.
- [ ] 6B.7 BLE Spam All: rotates through the above.
- [ ] 6B.8 Spoof Airtag: choose a tag from a previously-scanned list; a FindMy-capable test phone should report an unrecognized tracker.
- [ ] 6B.9 FindMy Sound: choose a tracker you own from the list; it should play its find-my sound.
- [ ] 6B.10 After each attack, stop it and re-run the plain BLE sniffer (6A.1) to confirm advertising actually stopped.
- [ ] 6B.11 Switch between a BLE test and a WiFi sniffer test a few times in a row. Expected: no crash or heap exhaustion — WiFi and BLE share RAM and radio time on this chip, so this transition is worth specifically watching.

## 7-8. SD-dependent features **(HW-UNKNOWN — whether SD works on this board/port at all)**

If SD does not work on this build, all items below should fail gracefully (menu item absent, or a clear "SD not available" message) rather than crashing. Record which.

- [ ] 7.1 Scan APs, Save APs, reboot, Load APs — list restored after reboot.
- [ ] 7.2 Add SSIDs, Save SSIDs, Clear SSIDs, Load SSIDs — restored.
- [ ] 7.3 Scan/spoof flow, Save Airtags, Load Airtags — restored.
- [ ] 8.1 SD File Browser: list root, open a folder, go Back, delete a file (with confirmation prompt).
- [ ] 8.2 Backup SPIFFS: files appear under `/spiffs` on the card; serial `backupstatus` reports files/bytes.
- [ ] 8.3 Restore SPIFFS: only restore a backup you just made; device behaves normally after.
- [ ] 8.4 Update Firmware: only attempt with a `.bin` built for this exact board — skip/mark N/A if you don't have one, since a mismatched image can brick the OTA slot.
- [ ] 8.5 Serial `ls /` lists the SD root.
