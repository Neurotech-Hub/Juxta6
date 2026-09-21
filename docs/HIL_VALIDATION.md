# HIL validation log (nRF54L15 Tag)

Hardware-developer ground-truth log for Juxta6 Tag bring-up apps. Fill rows as you run tests; leave current cells blank until PPK2 captures known-good hardware.

**Do not invent pass/fail or current numbers.** The Tag HW guide does not publish application-state current targets. Do not invent CS absolute accuracy thresholds — characterize first.

## Power / debug reminder

- Power with **CR2032** **or** external supply — **never both**.
- For current measurement, Nordic examples use **3.0 V**; cut `SB1` to insert a series meter between `VBAT` and `VDD`.
- Prefer **PPK2** / power analyzer over a DC meter for radio/sensor bursts.
- Program via nRF54L DK **DEBUG OUT**; DEBUG OUT does not power the Tag.
- Build/flash/RTT via **nRF Connect** (user-owned). Board: `nrf54l15tag/nrf54l15/cpuapp`. Snippet: `rtt-console`.
- Channel Sounding apps need **NCS ≥ 3.3.1** with Tag board support. Do **not** apply nRF54L15 DK CS antenna overlays (`gpio1 11–14` are Tag TWI).



## App validation matrix


| Test ID | App               | What it proves                           | Pass criteria (visual / RTT)                                                                                                                                                                                              | Date | NCS | HW rev (PCA20072) | Result (pass/fail/skip) | PPK2 I_avg / notes | Operator |
| ------- | ----------------- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---- | --- | ----------------- | ----------------------- | ------------------ | -------- |
| HIL-01  | `tag-blink`       | LED1 RGBW channels + BTN1 edge           | R→G→B→W cycle; `sw0 pressed` on RTT                                                                                                                                                                                       | 2026-09-14 | 3.3.4 |                   | pass                    |                    |          |
| HIL-02  | `tag-btn-magnet`  | BTN1 as MAG_INT (3 s / 10 s)             | <3 s reject; 3–10 s green slow; ≥10 s blue fast; RTT `hold_ms` / outcome                                                                                                                                                  | 2026-09-14 | 3.3.4 |                   | pass                    |                    |          |
| HIL-03  | `tag-sysoff`      | System OFF + BTN1 wake                   | Enters OFF; BTN1 wakes; green 1 s; re-shelves; RTT reset cause                                                                                                                                                            | 2026-09-14 | 3.3.4 |                   | pass                    | Disconnect debugger; remove from DEBUG OUT to test OFF/wake properly |          |
| HIL-04  | `tag-sensors-off` | BMI270 + BME688 probe then suspend       | RTT `[PROBE]` / `[SUSPEND]` ok; idle                                                                                                                                                                                      | 2026-09-14 | 3.3.4 |                   | pass                    | BME688 suspend ok; BMI270 PM -ENOSYS (no driver PM) |          |
| HIL-05  | `tag-adxl367`     | ADXL367 motion count; unused sensors off | RTT probe; `motion_count` rises when shaken; `temp_c` from DIE_TEMP; BMI/BME suspended                                                                                                                                                            | 2026-09-14 | 3.3.4 |                   | pass                    | BMI270 PM -ENOSYS (same as HIL-04); re-check `temp_c` after juxta_motion enhance |          |
| HIL-06  | `tag-flash`       | MX25L3233F (U8) last-sector R/W          | `[PROBE]` ~4 MiB; `[ERASE]`/`[WRITE]`/`[VERIFY]` PASS (last sector only). Re-run if modules swapped from MX25R6435.                                                                                                                                                  | 2026-09-17 | 3.3.4 |                   | pass                    | Was R6435 8 MiB HIL; modules now L3233 4 MiB — re-confirm probe size |          |
| HIL-07  | `tag-ble-adv`     | Connectable adv `JX_TAG`                 | Green slow blink advertising; solid green connected; RTT Connect/Disconnect                                                                                                                                               | 2026-09-14 | 3.3.4 |                   | pass                    | Adv restart deferred via workqueue |          |
| HIL-08  | `tag-id`          | Unique identity + mobile profile         | RTT `hwinfo_device_id`, `bt_id`, `juxta_name=JX_…`, `juxta_profile=mobile`; LED1 green brief                                                                                                                              | 2026-09-14 | 3.3.4 |                   | pass                    | juxta_name=JX_6D5FB8 |          |
| HIL-09  | `tag-discover`    | Two-tag coarse discovery                 | Both flash same image; each RTT `peer_seen id=JX_… rssi=…`; `peer_lost` after ~5 s absence; LED1 green while peer visible                                                                                                 | 2026-09-14 | 3.3.4 |                   | pass                    |                    |          |
| HIL-10  | `tag-cs`          | Mobile↔mobile CS both roles              | Two tags; LED green=auto / blue=initiator / red=reflector; RTT median `distance_m` + `ifft`/`phase_slope`/`rtt`/`samples` (window=9); `quality`/`status`; BTN1 force override; record known separations (characterization gate) | 2026-09-14 | 3.3.4 |                   | pass                    | Ranging works (Nordic-style realtime RD + median window). Likely better than RSSI at short range; value of a CS connection vs adv RSSI alone unclear given code/complexity cost. |          |
| HIL-11  | `tag-rssi-adv`    | Adv RSSI + dual RX antenna               | Same image; BTN1 advertiser(red) / scanner(blue); scanner RTT `rssi_pkt id=… seq=… rssi=… rx_ant=1\|2 …`; both antennas appear; no CS | 2026-09-14 | 3.3.4 |                   | pass                    | Both rx_ant=1 and 2 logged; ANT1 ~5 dB stronger than ANT2 in short-range capture (`data/tag-rssi.log`). |          |
| HIL-12  | `tag-vdd`         | SAADC internal VDD (CR2032)              | RTT `vdd_mv=… batt_pct~…` idle and during brief adv; ~2.7–3.3 V on coin cell (nRF54 needs GAIN_1_4 + 0.9 V ref — not GAIN_1) | 2026-09-17 | 3.3.4 |                   | pass                    | GAIN_1_4 + 0.9 V ref |          |
| HIL-13  | `tag-bme688`      | BME688 one-shot T + RH                   | RTT `[PROBE] bme688 ok`; `[SAMPLE] temp_c=… humidity_pct=…` (room ~15–35 °C / ~20–80 %RH); `[SUSPEND]` ok; brief green LED | 2026-09-21 | 3.3.4 |                   | pass                    |                    |          |




## Two-tag lab notes (HIL-09 / HIL-10 / HIL-11)

1. Flash the **same** app image to Tag A and Tag B.
2. Note each `juxta_name` from RTT (`tag-id` or boot of discover/cs/rssi-adv).
3. **HIL-09:** place tags nearby; confirm mutual `peer_seen` / `peer_lost`.
4. **HIL-10:** lower `JX_` name is initiator unless BTN1 selects `force_initiator` / `force_reflector`.
5. Record separations (e.g. 0.5 m / 1 m / 2 m) and observed `distance_m` — characterization only.
6. Failures: white LED (all channels) + non-zero `status`; abandon and rediscover.
7. **HIL-11:** one tag advertiser (red), one scanner (blue) via BTN1. Scanner logs every matching adv (`rssi_pkt` with `seq` + `rx_ant`). App overlay un-hogs SKY13348; scanner muxes ANT1/ANT2. Compare per-antenna RSSI vs HIL-10 CS complexity tradeoff.

### HIL-10 gate conclusion (2026-09-14)

- Channel Sounding **works** on Tag↔Tag (functional pass).
- Distance estimates are **likely better than RSSI** for short-range proximity.
- Product tradeoff left open: whether a connected CS session is worth the complexity versus advertising RSSI alone for this application. **HIL-11** characterizes the adv-RSSI / dual-antenna side of that tradeoff.



## Known-good currents (fill after first good PPK2 runs)


| State                  | App / condition                | I_avg | I_peak (optional) | Notes                            | Date | Operator |
| ---------------------- | ------------------------------ | ----- | ----------------- | -------------------------------- | ---- | -------- |
| System OFF (shelf)     | `tag-sysoff` after OFF         |       |                   |                                  |      |          |
| Sensors quiet          | `tag-sensors-off` idle         |       |                   | BMI + BME suspended; ADXL unused |      |          |
| ADXL-only              | `tag-adxl367` sampling         |       |                   | BMI + BME suspended              |      |          |
| Flash burst            | `tag-flash` erase/write window |       |                   | Short event                      |      |          |
| BLE advertising        | `tag-ble-adv` disconnected     |       |                   |                                  |      |          |
| BLE connected idle     | `tag-ble-adv` connected        |       |                   |                                  |      |          |
| Discovery adv+scan     | `tag-discover`                 |       |                   |                                  |      |          |
| Adv RSSI dual-antenna  | `tag-rssi-adv` scanner         |       |                   |                                  |      |          |
| CS initiator procedure | `tag-cs` initiator             |       |                   | Short event                      |      |          |
| CS reflector armed     | `tag-cs` reflector             |       |                   |                                  |      |          |




## Later (not in this wave)


| ID  | App / topic            | Intent                                                                   |
| --- | ---------------------- | ------------------------------------------------------------------------ |
| —   | `juxta6-0-prod` M3     | NOR CSV + Filename/File Transfer + MCUboot SMP DFU (`6.4.0`, schema v8, MX25L3233 4 MiB). See app README. Validate companion LIST/pull + clearMemory + Device Manager DFU. |
| —   | Encounter Manager      | Qualify / arbitrate / cooldown / budget (opportunistic, not a scheduler) |
| —   | 3-tag HIL              | Multi-peer encounter manager behavior                                    |
| —   | Anchor profile         | Same CS layer; policy-only difference                                    |
| —   | MCUboot / SMP DFU      | M3 — ≥10 s magnet → SMP BLE; nRF Device Manager uploads; ≥3 s hold → shelf |
| —   | CS in prod             | M4 — optional; M2 uses advertising RSSI                                  |




## Hardware notes

- **BTN1** (`sw0` / `P0.00`) stands in for Juxta5-8 MAG_INT (and CS role override in `tag-cs`).
- **LED1** RGB only; LED2 footprint unused.
- Motion path: **ADXL367** only. BMI270 / BME688 stay shut down outside their probe fixtures (`tag-sensors-off`, `tag-bme688`).
- External flash **U8**: project modules use **MX25L3233F** (32 Mbit / 4 MiB); stock Nordic BOM may list MX25R6435. CS `P2.05`.
- Identity: `JX_` + last three bytes of BLE public identity (same rule as Juxta5-8 ble-range).
- Channel Sounding is isolated in `lib/juxta_range/`. Apps must not call Nordic CS/RAS APIs directly.
- No FUEL ADC on Tag (CR2032 direct).



## References

- [nRF54L15_Tag_Hardware_Reference.md](nRF54L15_Tag_Hardware_Reference.md)
- [agents/JUXTA-SPEC.md](../agents/JUXTA-SPEC.md)
- [agents/BRINGUP.md](../agents/BRINGUP.md)
- [reference/juxta5-8/REFERENCE.md](../reference/juxta5-8/REFERENCE.md)
- [lib/juxta_range/README.md](../lib/juxta_range/README.md)

