# SenseCAP Indicator — Marauder Test Plan (Part 3: CLI, memory/stability)

Continuation of `SENSECAP_TEST_PLAN.md` and `_part2.md`. Same marking convention.
Same scope note as Part 2 applies here: only run transmitting commands against
your own equipment.

## 9. CLI over USB serial

Commands are typed directly with no prefix, e.g. `help`. Use `stopscan` to end any running scan/attack before starting the next line, unless the step says otherwise.

- [ ] 9.1 `help` — full command list with syntax.
- [ ] 9.2 `info` / `info -a 0` — AP list / single AP detail.
- [ ] 9.3 `scanall` then `stopscan` — starts and cleanly stops an AP+station scan.
- [ ] 9.4 Each of: `sniffbeacon`, `sniffprobe`, `sniffdeauth`, `sniffpmkid -c 6`, `sniffraw`, `sniffpwn`, `sniffpinescan`, `sniffmultissid`, `sniffsae` — starts, prints results, `stopscan` halts it.
- [ ] 9.5 `channel` then `channel -s 6` — reports then sets the channel.
- [ ] 9.6 `list`, `select -a 0`, `ssid -a -g 5`, `ssid -l`, `clearlist -a` — list management commands keep consistent indexes.
- [ ] 9.7 Each attack type via `attack -t <type>` (beacon -r, deauth, probe, rickroll, quiet, csa, sae, badmsg, sleep as listed in `help`) — starts, `stopscan` halts. Only against your own test AP/clients.
- [ ] 9.8 `blespam -t sourapple`, `sniffbt -t airtag`, `sniffskim`, `findmy -t 0`, `spoofat -t 0` — BLE equivalents; only against your own test devices.
- [ ] 9.9 `join -a 0 -p <password>` and `join -s` — joins a network by index+password, or by saved profile.
- [ ] 9.10 `pingscan`, `arpscan`, `portscan -a -t 0`, `portscan -s http` — scanner results print.
- [ ] 9.11 `evilportal -c start` — starts the captive portal.
- [ ] 9.12 `packetcount`, `mactrack`, `foxhunt -w 0` — each runs without error.
- [ ] 9.13 `reboot` — device restarts and comes back up.
- [ ] 9.14 `led -s ff0000` — command is accepted; no visible effect expected (no addressable LED on this board) and no crash.
- [ ] 9.15 `geofence list` — lists the 5 geofence slots (empty or configured).
- [ ] 9.16 `protocolinfo` — prints protocol/machine-interface info.
- [ ] 9.17 An unrecognized command, e.g. `foo` — prints an error/help hint, does not crash.
- [ ] 9.18 Start a scan from serial, then operate the touch menu while it runs. Expected: consistent state between the two interfaces, no lockup.

## 10. Memory and stability soak

- [ ] 10.1 Record free RAM at idle (Device Info or status bar): ______
- [ ] 10.2 Run 10 start/stop cycles each of: Beacon Sniff, Probe Sniff, Bluetooth Sniffer. Expected: free RAM returns close to the idle value every time, no steady downward drift.
- [ ] 10.3 Switch between a WiFi mode and a BLE mode 10 times in a row. Expected: no crash, no "not enough memory" warnings.
- [ ] 10.4 Run Beacon Sniff with SavePCAP for 30 minutes (if SD works). Expected: keeps running, pcap file grows, no reboot.
- [ ] 10.5 For any reboot or crash seen anywhere in this test plan: copy the full serial crash backtrace and note which menu item/command was active when it happened. This is the single most useful piece of information for tracking down a bug, so don't skip it even if the rest of that section otherwise passed.

## Wrap-up

- [ ] Summarize: how many items passed / failed / partial, and list the **(HW-UNKNOWN)** items separately since those are the ones most likely to need a code or wiring fix rather than being expected behavior.
