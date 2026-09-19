# juxta6-0-prod (M2)

Juxta-style firmware for nRF54L15 Tag: shelf / magnet / **Hublink GATT sync** /
dual-antenna advertising RSSI / live ADXL+VDD vitals / **MX25L3233 NOR CSV**.

External flash is Macronix **MX25L3233FZBI-08G-TR** (32 Mbit / 4 MiB) on Tag U8
SPI (`P2.01/02/04`, CS `P2.05`). Schema **`jxta-nor-csv-v7`** (JXV/JXB rows use
day-relative seconds; JXB drops the observer column and the `JX_` peer prefix;
JXS adds `latitude,longitude` from the last gateway sync. Devices upgrading
need a `clearMemory`/format). Layout:

| Region | Start | Size |
|--------|-------|------|
| JXS | `0x000000` | 64 KB |
| JXV | `0x010000` | 1 MB |
| JXB | `0x110000` | 2.875 MB |
| Checkpoint ring | `0x3F0000` | 64 KB |

RTT still mirrors `JXS` / `JXB` / `JXV` for debugger bring-up. Filename / File
Transfer are live (LIST + chunked offload). **No MCUboot/DFU** yet (BTN1 ≥10 s =
LED cue + RTT/`dfu_requested` only). **No Channel Sounding.**

## Debugger / RTT

Every POR / soft reboot waits **500 ms** (contact settle — do not remove; temporarily
lengthened to validate reseat hypothesis). ADXL / BME / BMI / SPI NOR are
`zephyr,deferred-init` so they are **not** probed during `POST_KERNEL`;
`device_init()` runs only after that settle.

A single **~20 ms white LED chirp** runs in `enter_shelf()` only — covers cold
boot→shelf, prod magnet escape, and gateway reset (debugger soft-reboot chirps
once before reboot; no second chirp at `main`).

With J-Link attached (`DCB`/`CoreDebug` `C_DEBUGEN`), System OFF is **simulated**:
LED off while waiting for BTN1; shelf entry does a **soft reboot** instead of
`sys_poweroff()` so RTT stays connected. Detach the debugger to exercise real
System OFF / pin wake.

A hardware **watchdog** (`wdt31`, 60 s) arms after bring-up and is fed from all
long-lived loops (paused while halted by the debugger). Fatal errors reboot
(`CONFIG_RESET_ON_FATAL_ERROR`).

Init fault (1 s on / 1 s off, no RTT needed) — shown only after **3 automatic
reboot retries** (~1 s apart) fail; a transient probe failure self-recovers:

| Color | Failed init |
| ----- | ----------- |
| Red (1 s) | SPI NOR |
| Blue | Antenna |
| Green | ADXL367 |
| White | Late `hardware_ready` guard |

**UVLO / replace battery** (distinct from NOR red fault): after settle, before
sensors/NOR/BT — and on each vitals sample — if VDD **&lt; 2500 mV** for 3
samples → sticky **short red chirp** (~40 ms) / **~2 s off**. Skipped with
debugger attached. No consequential I/O in lockout.

Cold CR2032 boot: **500 ms** settle → VDD/UVLO gate → deferred `device_init`
(ADXL/NOR/…) → antenna → `bt_enable` → shelf (one white chirp) or sync wake.

## Production radio

`scan_interval_s` / `adv_interval_s` are **cadence** (seconds between bursts), not
burst length. Defaults: **scan every 30 s**, **adv every 5 s** (0 = disable that
modality). Each burst is fixed ~**1 s** (scan = 500 ms ANT1 + 500 ms ANT2;
adv = 1000 ms non-connectable). Never both at once; scan wins if both due.

## Build / flash (user)

- Board: `nrf54l15tag/nrf54l15/cpuapp`
- Snippet: `rtt-console`
- App: `applications/juxta6-0-prod`

## Companion flow

1. Power on → device shelves (System OFF), or `[DBG] Simulated shelf` with debugger.
2. Hold **BTN1** 3–10 s → slow green blink, connectable Hublink advertising
   (`JX_XXXXXX` via `bt_set_name` + scan response; Hublink UUID in ADV).
3. Connect; peripheral negotiates **MTU 247**. Read **Node** (`firmwareVersion`
   `6.2.0`, `memoryLevel` from NOR fill, refreshed at vitals cadence); write
   **Gateway** JSON with `"timestamp": <unix>` plus optional `latitude` /
   `longitude` / `tempC` (and optional settings).
4. Disconnect → 5× blink → production (LED off).
5. During production, hold **BTN1** ≥3 s to shelf: **red+blue** while holding,
   LEDs off at 3 s (commit) → **5× green blink** → **1 s** release debounce →
   white chirp → System OFF / debugger soft-reboot.
6. RTT shows `JXB` / `JXV`; companion **LIST** / pull dated `JX{S|V|B}YYYYMMDD.csv`.
7. Gateway `clearMemory` erases CSV regions (deferred workqueue) then empty LIST.

iOS companion: [companion/iOS](../../companion/iOS) (`Juxta6.xcodeproj`, firmware `6.x` / schema v7).

## RTT line shapes

```
JXS unix=<u> event=<name>
JXB unix=<u> peer=JX_… rssi=<dBm>
JXV unix=<u> motion=<n> batt_mv=<mv> temp_c=<c>
```

## Later milestones

- M3: MCUboot / SMP DFU (wire DFU — port from Juxta 5.8)
- M4: Encounter Manager + optional CS
- Temperature: BME688 optional; gateway `tempC` already soft-calibrates ADXL die temp
