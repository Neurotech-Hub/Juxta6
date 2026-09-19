# Juxta6

Monorepo for the **Juxta6** wireless Tag platform (nRF54L15): production firmware,
hardware bring-up fixtures, an iOS companion, and a browser plotter for daily CSV
logs (`jxta-nor-csv-v6`).

| Area | What |
| --- | --- |
| Firmware | [`applications/juxta6-0-prod`](applications/juxta6-0-prod) — shelf / Hublink sync / NOR CSV / vitals |
| HIL | Isolated Tag apps under [`applications/`](applications/) for LED, sensors, flash, BLE, CS |
| Companion | [`companion/iOS`](companion/iOS) — **Juxta6** Xcode app (connect, settings, file transfer) |
| Plotter | [`plotter/`](plotter/) — **Juxta6 Plotter** ([live](https://neurotech-hub.github.io/Juxta6/)) |
| Libs | [`lib/`](lib/) — ID, motion (ADXL367), VDD, Channel Sounding isolation |
| Spec | [agents/JUXTA-SPEC.md](agents/JUXTA-SPEC.md) |

**License:** proprietary — see [LICENSE](LICENSE). Reuse (academic or commercial) requires explicit written permission from Neurotech Hub.

## Platform

| Item | Value |
| --- | --- |
| Board target | `nrf54l15tag/nrf54l15/cpuapp` |
| NCS | **v3.3.1 or newer** (Tag board + Channel Sounding) |
| Console | Segger RTT via `rtt-console` snippet (no UART on Tag by default) |
| TrustZone / FLPR | Out of scope (`/ns` and FLPR not used) |

### Power and debug

- Program/debug the Tag through an **nRF54L DK DEBUG OUT** header.
- Power the Tag with a **CR2032** **or** **3.3 V on `VDD SWD0`** — **never both**.
- DEBUG OUT does **not** power the Tag by default.
- Current measurement: prefer PPK2; cut `SB1` for series meter; Nordic examples use **3.0 V**.

### Hardware contract (Juxta mapping)

| Role | Tag |
| --- | --- |
| Magnet stand-in | **BTN1** (`sw0` / `P0.00`) |
| Status LED | **LED1** RGB only (`led1_red` / `led1_green` / `led1_blue`) |
| Motion count | **ADXL367** (BMI270 / BME688 stay shut down for now) |
| External flash | **MX25L3233F** U8 (32 Mbit / 4 MiB; project-fitted) |
| Identity | `JX_` + last 3 bytes of BLE public address |
| FUEL ADC | Not present — do not port |
| Channel Sounding | Dual antenna + SKY13348; SDK behind `lib/juxta_range` |

## Layout

```text
.
├── README.md
├── agents/
│   ├── BRINGUP.md
│   ├── RULES.md
│   └── JUXTA-SPEC.md
├── lib/
│   ├── juxta_id/                  # JX_XXXXXX helpers
│   ├── juxta_motion/              # ADXL367 XYZ + DIE_TEMP poll
│   ├── juxta_vdd/                 # SAADC VDD mV helper
│   └── juxta_range/               # CS/RAS isolation layer
├── docs/
│   ├── HIL_VALIDATION.md
│   └── ...
├── companion/
│   └── iOS/                       # Juxta6 Xcode companion (Hublink + schema v6)
├── plotter/                       # Juxta6 Plotter (static; GitHub Pages)
├── reference/juxta5-8/            # Frozen — do not west-build
└── applications/
    ├── tag-blink/ … tag-ble-adv/  # HIL-01..07 hardware fixtures
    ├── tag-vdd/                   # HIL-12 SAADC VDD
    ├── tag-id/                    # HIL-08 identity
    ├── tag-discover/              # HIL-09 two-tag discovery
    ├── tag-cs/                    # HIL-10 mobile↔mobile CS
    ├── tag-rssi-adv/              # HIL-11 adv RSSI + dual RX antenna
    └── juxta6-0-prod/             # M2 product image (Hublink + NOR CSV + RTT)
```

## Build and flash (user)

Build and flash with **nRF Connect for VS Code / Cursor**. Agents do not run `west` or the debugger.

- Board: `nrf54l15tag/nrf54l15/cpuapp`
- Application: `applications/<app>`
- Extra: **RTT Console** snippet (`rtt-console`)

```bash
west build -b nrf54l15tag/nrf54l15/cpuapp --snippet rtt-console applications/tag-id
west flash
```

Validation log: [docs/HIL_VALIDATION.md](docs/HIL_VALIDATION.md).

## HIL app series

### Wave A — hardware fixtures

| Order | App | Intent |
| --- | --- | --- |
| 1 | `tag-blink` | LED1 R→G→B→W; BTN1 press log |
| 2 | `tag-btn-magnet` | BTN1 mimics MAG_INT (3 s / 10 s) |
| 3 | `tag-sysoff` | System OFF + BTN1 wake |
| 4 | `tag-sensors-off` | Probe then suspend BMI + BME |
| 5 | `tag-adxl367` | ADXL367 motion + `temp_c`; BMI/BME off |
| 6 | `tag-flash` | MX25L3233 last sector only (4 MiB) |
| 7 | `tag-ble-adv` | Connectable adv smoke |
| 12 | `tag-vdd` | SAADC VDD `vdd_mv` / CR2032 `%` estimate |

### Wave B — mobile↔mobile ranging (pre-prod)

| Order | App | Intent |
| --- | --- | --- |
| 8 | `tag-id` | hwinfo + BLE ID → `JX_XXXXXX`; mobile profile |
| 9 | `tag-discover` | Identical dual adv+scan; `peer_seen` / `peer_lost` (no CS) |
| 10 | `tag-cs` | One image, both CS roles; LED green=auto / blue=initiator / red=reflector; BTN1 override |
| 11 | `tag-rssi-adv` | Advertiser(red)/scanner(blue); per-packet RSSI + seq + rx_ant (ANT1/ANT2); no CS |

**Two-tag lab:** flash the same image to both tags. CS distance is logged for characterization — no accuracy gate yet. Do not copy DK CS antenna overlays onto Tag.

### Wave C — product (M2)

| App | Intent |
| --- | --- |
| `juxta6-0-prod` | Shelf / magnet / Hublink sync / dual-ant max-RSSI / live vitals / **MX25L3233 NOR CSV** + Filename/File Transfer; RTT mirrors. See [applications/juxta6-0-prod/README.md](applications/juxta6-0-prod/README.md). |
| Companion | [companion/iOS](companion/iOS) — **Juxta6** Xcode app (firmware 6.x / `jxta-nor-csv-v6`). Open `Juxta6.xcodeproj` (not west). |
| Plotter | [plotter/](plotter/) — **Juxta6 Plotter** ([https://neurotech-hub.github.io/Juxta6/](https://neurotech-hub.github.io/Juxta6/)). |

## Later milestones

| Milestone | Intent |
| --- | --- |
| M3 | MCUboot + SMP DFU (wire DFU — port from Juxta 5.8) |
| M4 | Encounter Manager + optional CS |
| — | 3-tag HIL; anchor policy |
| — | Temperature: retain BME688, or sync temp from iPhone to calibrate ADXL/IMU (~10 °C skew between devices today) |

## Non-goals (M2)

No MCUboot/DFU (cue only), Channel Sounding in prod, Encounter Manager, LED2/BTN2, FUEL ADC, or DK CS antenna overlays on Tag TWI pins.

## Links

- [Nordic nRF54L15 Tag intro](https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_tag/page/ug/nrf54l15_tag/intro.html)
- [Zephyr board: nRF54L15 TAG](https://docs.zephyrproject.org/latest/boards/nordic/nrf54l15tag/doc/index.html)
- [docs/nRF54L15_Tag_Hardware_Reference.md](docs/nRF54L15_Tag_Hardware_Reference.md)
- [agents/JUXTA-SPEC.md](agents/JUXTA-SPEC.md)
- [lib/juxta_range/README.md](lib/juxta_range/README.md)
- [plotter/](plotter/) — Juxta6 Plotter ([GitHub Pages](https://neurotech-hub.github.io/Juxta6/))
- [LICENSE](LICENSE) — proprietary; permission required for reuse
- [reference/juxta5-8/REFERENCE.md](reference/juxta5-8/REFERENCE.md)
