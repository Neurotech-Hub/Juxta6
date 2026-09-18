# juxta6-0-prod (M2)

Juxta-style firmware for nRF54L15 Tag: shelf / magnet / **Hublink GATT sync** /
dual-antenna advertising RSSI / live ADXL+VDD vitals / **MX25L3233 NOR CSV**.

External flash is Macronix **MX25L3233FZBI-08G-TR** (32 Mbit / 4 MiB) on Tag U8
SPI (`P2.01/02/04`, CS `P2.05`). Layout matches Juxta5-8 (`jxta-nor-csv-v5`):

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
`sys_poweroff()` so RTT stays connected (same pattern as Juxta5-8). Detach the
debugger to exercise real System OFF / pin wake.

Init fault (1 s on / 1 s off, no RTT needed):

| Color | Failed init |
| ----- | ----------- |
| Red | SPI NOR (`juxta_log_init`) |
| Blue | Antenna (`juxta_antenna_init`) |
| Green | ADXL367 (`juxta_motion_init`) |
| White | Late `hardware_ready` guard |

Cold CR2032 boot: **500 ms** settle → deferred `device_init` (ADXL/NOR/…) →
antenna → `bt_enable` → shelf (one white chirp) or sync wake.

## Production radio (Juxta5-8 semantics)

`scan_interval_s` / `adv_interval_s` are **cadence** (seconds between bursts), not
burst length. Defaults match 5-8: **scan every 30 s**, **adv every 5 s** (0 =
disable that modality). Each burst is fixed ~**1 s** (scan = 500 ms ANT1 + 500 ms
ANT2; adv = 1000 ms non-connectable). Never both at once; scan wins if both due.

NVS may still hold older 1/1 values from prior builds — write Gateway
`scanInterval`/`advInterval` or clear settings to pick up new defaults.

## Build / flash (user)

- Board: `nrf54l15tag/nrf54l15/cpuapp`
- Snippet: `rtt-console`
- App: `applications/juxta6-0-prod`

## Companion flow

1. Power on → device shelves (System OFF), or `[DBG] Simulated shelf` with debugger.
2. Hold **BTN1** 3–10 s → slow green blink, connectable Hublink advertising
   (`JX_XXXXXX` via `bt_set_name` + scan response; Hublink UUID in ADV).
3. Connect; peripheral negotiates **MTU 247**. Read **Node** (`firmwareVersion`
   `6.0.0-nor`, `memoryLevel` from NOR fill); write **Gateway** JSON with
   `"timestamp": <unix>` (and optional settings).
4. Disconnect → 5× blink → production (LED off).
5. During production, hold **BTN1** ≥3 s to shelf: **red+blue** while holding,
   LEDs off at 3 s (commit) → **5× green blink** → **1 s** release debounce →
   white chirp → System OFF / debugger soft-reboot.
6. RTT shows `JXB` / `JXV`; companion **LIST** / pull dated `JX{S|V|B}YYYYMMDD.csv`.
7. Gateway `clearMemory` erases CSV regions (deferred workqueue) then empty LIST.

If the iOS app still filters on `firmwareVersion` starting with `5.8`, allowlist
`6.0` or temporarily rebuild with a `5.8.x-nor` string after connect succeeds.

## RTT line shapes

```
JXS unix=<u> event=<name>
JXB unix=<u> observer=JX_… peer=JX_… rssi=<dBm>
JXV unix=<u> motion=<n> batt_mv=<mv> temp_c=<c>
```

## Later milestones

- M3: MCUboot / SMP DFU
- M4: Encounter Manager + optional CS
