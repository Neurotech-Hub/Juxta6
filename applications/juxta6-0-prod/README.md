# juxta6-0-prod (M3)

Juxta-style firmware for nRF54L15 Tag: shelf / magnet / **Hublink GATT sync** /
dual-antenna advertising RSSI / ADXL motion + BME688 T/RH + VDD vitals /
**MX25L3233 NOR CSV** / **MCUboot SMP BLE DFU** (nRF Device Manager).

External flash is Macronix **MX25L3233FZBI-08G-TR** (32 Mbit / 4 MiB) on Tag U8
SPI (`P2.01/02/04`, CS `P2.05`). Schema **`jxta-nor-csv-v8`**:

| File | Columns |
|------|---------|
| JXV | `sec,motion,batt_v,temp_c,humidity` — day-relative `sec`; `temp_c` / `humidity` from BME688 one-shot at one decimal (`22.9,39.1`); empty cells if sample fails |
| JXB | `sec,peer_id,rssi` — no observer column; peer IDs without `JX_` prefix |
| JXS | absolute `unix` events + `latitude,longitude` from gateway sync |

## NOR layout and JXV capacity

| Region | Start | Size |
|--------|-------|------|
| JXS | `0x000000` | 64 KB |
| JXV | `0x010000` | **1 MB** |
| JXB | `0x110000` | 2.875 MB |
| Checkpoint ring | `0x3F0000` | 64 KB |

**Vitals cadence** is hard-floored at **60 s** (Gateway cannot request denser). At
60 s → **1 440 rows/day**. With `XX.X` temp + humidity (~23 B/row typical),
JXV holds roughly **~30 days** of vitals before the region latches full; the
file catalog (`JUXTA_MAX_FILES` = 72 ≈ 24 days × 3 types) targets a **~22-day**
deployment with margin. Faster vitals are rejected so the 1 MiB budget stays
valid. `memoryLevel` is the **max** fill across JXS/JXV/JXB (a full JXV reports
nearly 100% even if JXB is empty).

RTT still mirrors `JXS` / `JXB` / `JXV` for debugger bring-up. Filename / File
Transfer are live (LIST + chunked offload). **DFU:** BTN1 ≥10 s → MCUboot SMP
BLE advertising for **nRF Device Manager** (fast blue blink; ≥3 s hold returns
to shelf). **No Channel Sounding.**

## Debugger / RTT

Every POR / soft reboot waits **500 ms** (contact settle — do not remove; temporarily
lengthened to validate reseat hypothesis). ADXL / BME / BMI / SPI NOR are
`zephyr,deferred-init` so they are **not** probed during `POST_KERNEL`;
`device_init()` runs only after that settle. Production vitals **resume → sample
→ suspend** BME688 each period (motion init leaves it suspended).

**Shelf mode** stays awake (no System OFF): radio and motion stop, then a
**~20 ms white LED chirp every 5 s** so the Tag is visibly waiting for a
connection. A valid BTN1 hold (≥3 s sync, ≥10 s DFU) soft-reboots into the
normal sync/DFU path via a `__noinit` wake handoff. Entry cue chirp covers cold
boot→shelf, prod magnet escape, gateway reset, and DFU exit.

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
(ADXL/NOR/…) → antenna → `bt_enable` → shelf (white chirps every 5 s).

## Production radio

`scan_interval_s` / `adv_interval_s` are **cadence** (seconds between bursts), not
burst length. Defaults: **scan every 30 s**, **adv every 5 s** (0 = disable that
modality). Each burst is fixed ~**1 s** (scan = 500 ms ANT1 + 500 ms ANT2;
adv = 1000 ms non-connectable). Never both at once; scan wins if both due.

## Build / flash (user)

- Board: `nrf54l15tag/nrf54l15/cpuapp`
- Snippet: `rtt-console`
- App: `applications/juxta6-0-prod`
- **Sysbuild / MCUboot required** (`sysbuild.conf`, Partition Manager **off**).
  In nRF Connect: enable Sysbuild so MCUboot + app are flashed. Slots come from
  Tag DTS (`boot` 64 KB, `slot0`/`slot1` 664 KB, `storage` 36 KB). First
  MCUboot bring-up — or a switch from a Partition Manager image — needs a full
  chip erase / recover.
- DFU images are **unsigned** (`CONFIG_BOOT_SIGNATURE_TYPE_NONE`) for local
  Device Manager uploads.

## DFU (nRF Device Manager)

This zip is the **app slot** only. First-time programming (or after erase) still
uses Sysbuild `merged.hex` so MCUboot is on the device.

After a successful Sysbuild in nRF Connect, copy a versioned package out of
`build/` (do not commit that folder):

```bash
applications/juxta6-0-prod/scripts/export-dfu.sh
```

Writes `dist/juxta6-0-prod-<version>.zip` from `JUXTA_FIRMWARE_VERSION` in
[`src/juxta_prod.h`](src/juxta_prod.h). Bump that string before exporting a new
image. Commit `dist/` when you want to share the zip; `build/` stays gitignored.

1. From shelf, hold **BTN1 ≥10 s** (LED off at 3 s commit cue). Battery must be
   ≥2700 mV or the wake falls through to normal sync.
2. Tag: 3× blink → fast blue blink; advertises SMP UUID as `JX_XXXXXX`.
3. Open **nRF Device Manager**, connect, upload `dist/juxta6-0-prod-<version>.zip`.
   MCUboot swaps slots on reset.
4. Hold **BTN1 ≥3 s** to leave DFU → shelf (white chirps every 5 s).

## Companion flow

1. Power on → device shelves (white chirp ~20 ms every 5 s).
2. Hold **BTN1** 3–10 s → soft reboot → slow green blink, connectable Hublink
   advertising (`JX_XXXXXX` via `bt_set_name` + scan response; Hublink UUID in ADV).
3. Connect; peripheral negotiates **MTU 247**. Read **Node** (`firmwareVersion`
   `6.4.0`, `memoryLevel` from NOR fill, refreshed at vitals cadence); write
   **Gateway** JSON with `"timestamp": <unix>` plus optional `latitude` /
   `longitude` (and optional settings). Open-Meteo outdoor temp is display-only
   on the phone — not sent to the tag.
4. Disconnect → 5× blink → production (LED off).
5. During production, hold **BTN1** ≥3 s to shelf: **red+blue** while holding,
   LEDs off at 3 s (commit) → **5× green blink** → **1 s** release debounce →
   shelf chirps resume.
6. RTT shows `JXB` / `JXV`; companion **LIST** / pull dated `JX{S|V|B}YYYYMMDD.csv`.
7. Gateway `clearMemory` erases CSV regions (deferred workqueue) then empty LIST.

iOS companion: [companion/iOS](../../companion/iOS) (`Juxta6.xcodeproj`, firmware `6.4.0` / schema v8).

## RTT line shapes

```
JXS unix=<u> event=<name>
JXB unix=<u> peer=JX_… rssi=<dBm>
JXV unix=<u> motion=<n> batt_mv=<mv> temp_c=<c> humidity=<rh>
```

## Later milestones

- M4: Encounter Manager + optional CS
