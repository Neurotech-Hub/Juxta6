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
| HIL-01  | `tag-blink`       | LED1 RGB channels + BTN1 edge            | R→G→B cycle; `sw0 pressed` on RTT                                                                                                                                                                                         |      |     |                   |                         |                    |          |
| HIL-02  | `tag-btn-magnet`  | BTN1 as MAG_INT (3 s / 10 s)             | <3 s reject; 3–10 s green slow; ≥10 s blue fast; RTT `hold_ms` / outcome                                                                                                                                                  |      |     |                   |                         |                    |          |
| HIL-03  | `tag-sysoff`      | System OFF + BTN1 wake                   | Enters OFF; BTN1 wakes; green 1 s; re-shelves; RTT reset cause                                                                                                                                                            |      |     |                   |                         |                    |          |
| HIL-04  | `tag-sensors-off` | BMI270 + BME688 probe then suspend       | RTT `[PROBE]` / `[SUSPEND]` ok; idle                                                                                                                                                                                      |      |     |                   |                         |                    |          |
| HIL-05  | `tag-adxl367`     | ADXL367 motion count; unused sensors off | RTT probe; `motion_count` rises when shaken; BMI/BME suspended                                                                                                                                                            |      |     |                   |                         |                    |          |
| HIL-06  | `tag-flash`       | MX25R6435F (U8) last-sector R/W          | `[PROBE]` ~8 MiB; `[ERASE]`/`[WRITE]`/`[VERIFY]` PASS (last sector only)                                                                                                                                                  |      |     |                   |                         |                    |          |
| HIL-07  | `tag-ble-adv`     | Connectable adv `JX_TAG`                 | Green slow blink advertising; solid green connected; RTT Connect/Disconnect                                                                                                                                               |      |     |                   |                         |                    |          |
| HIL-08  | `tag-id`          | Unique identity + mobile profile         | RTT `hwinfo_device_id`, `bt_id`, `juxta_name=JX_…`, `juxta_profile=mobile`; LED1 green brief                                                                                                                              |      |     |                   |                         |                    |          |
| HIL-09  | `tag-discover`    | Two-tag coarse discovery                 | Both flash same image; each RTT `peer_seen id=JX_… rssi=…`; `peer_lost` after ~5 s absence; LED1 green while peer visible                                                                                                 |      |     |                   |                         |                    |          |
| HIL-10  | `tag-cs`          | Mobile↔mobile CS both roles              | Two tags; lower ID initiates (green) / higher reflects (blue); RTT `distance_m` / `quality` / `status`; BTN1 force override exercises both roles on one board; try several known separations (log only, no accuracy gate) |      |     |                   |                         |                    |          |




## Two-tag lab notes (HIL-09 / HIL-10)

1. Flash the **same** app image to Tag A and Tag B.
2. Note each `juxta_name` from RTT (`tag-id` or boot of discover/cs).
3. **HIL-09:** place tags nearby; confirm mutual `peer_seen` / `peer_lost`.
4. **HIL-10:** lower `JX_` name is initiator unless BTN1 selects `force_initiator` / `force_reflector`.
5. Record separations (e.g. 0.5 m / 1 m / 2 m) and observed `distance_m` — characterization only.
6. Failures: red LED + non-zero `status`; abandon and rediscover.



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
| CS initiator procedure | `tag-cs` initiator             |       |                   | Short event                      |      |          |
| CS reflector armed     | `tag-cs` reflector             |       |                   |                                  |      |          |




## Later (not in this wave)


| ID  | App / topic            | Intent                                                                   |
| --- | ---------------------- | ------------------------------------------------------------------------ |
| —   | Encounter Manager      | Qualify / arbitrate / cooldown / budget (opportunistic, not a scheduler) |
| —   | Encounter log to flash | Persist peer ID + distance + quality across reset                        |
| —   | 3-tag HIL              | Multi-peer encounter manager behavior                                    |
| —   | Anchor profile         | Same CS layer; policy-only difference                                    |
| —   | `tag-prod`             | Hublink + Juxta5-8 contracts after HIL passes                            |




## Hardware notes

- **BTN1** (`sw0` / `P0.00`) stands in for Juxta5-8 MAG_INT (and CS role override in `tag-cs`).
- **LED1** RGB only; LED2 footprint unused.
- Motion path: **ADXL367** only. BMI270 / BME688 stay shut down outside their probe fixtures.
- External flash **U8** is project-fitted; stock Nordic BOM leaves it empty.
- Identity: `JX_` + last three bytes of BLE public identity (same rule as Juxta5-8 ble-range).
- Channel Sounding is isolated in `lib/juxta_range/`. Apps must not call Nordic CS/RAS APIs directly.
- No FUEL ADC on Tag (CR2032 direct).



## References

- [nRF54L15_Tag_Hardware_Reference.md](nRF54L15_Tag_Hardware_Reference.md)
- [agents/JUXTA-SPEC.md](../agents/JUXTA-SPEC.md)
- [agents/BRINGUP.md](../agents/BRINGUP.md)
- [reference/juxta5-8/REFERENCE.md](../reference/juxta5-8/REFERENCE.md)
- [lib/juxta_range/README.md](../lib/juxta_range/README.md)

