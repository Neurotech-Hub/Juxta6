# Juxta6-nRF

nRF Connect SDK workspace for the **nRF54L15 Tag**. Isolated HIL apps establish ground-truth fixtures for opportunistic **mobile↔mobile** ranging before production firmware.

Architecture brief: [agents/JUXTA-SPEC.md](agents/JUXTA-SPEC.md).

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
| External flash | **MX25R6435F** U8 (project-fitted; not stock BOM) |
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
│   └── juxta_range/               # CS/RAS isolation layer
├── docs/
│   ├── HIL_VALIDATION.md
│   └── ...
├── reference/juxta5-8/            # Frozen — do not west-build
└── applications/
    ├── tag-blink/ … tag-ble-adv/  # HIL-01..07 hardware fixtures
    ├── tag-id/                    # HIL-08 identity
    ├── tag-discover/              # HIL-09 two-tag discovery
    ├── tag-cs/                    # HIL-10 mobile↔mobile CS
    └── tag-rssi-adv/              # HIL-11 adv RSSI + dual RX antenna
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
| 5 | `tag-adxl367` | ADXL367 motion count; BMI/BME off |
| 6 | `tag-flash` | MX25R last sector only |
| 7 | `tag-ble-adv` | Connectable adv smoke |

### Wave B — mobile↔mobile ranging (pre-prod)

| Order | App | Intent |
| --- | --- | --- |
| 8 | `tag-id` | hwinfo + BLE ID → `JX_XXXXXX`; mobile profile |
| 9 | `tag-discover` | Identical dual adv+scan; `peer_seen` / `peer_lost` (no CS) |
| 10 | `tag-cs` | One image, both CS roles; LED green=auto / blue=initiator / red=reflector; BTN1 override |
| 11 | `tag-rssi-adv` | Advertiser(red)/scanner(blue); per-packet RSSI + seq + rx_ant (ANT1/ANT2); no CS |

**Two-tag lab:** flash the same image to both tags. CS distance is logged for characterization — no accuracy gate yet. Do not copy DK CS antenna overlays onto Tag.

## Later (not implemented)

| Topic | Intent |
| --- | --- |
| Encounter Manager | Opportunistic qualify / arbitrate / cooldown / budget (not a scheduler) |
| Encounter logger | Flash persistence of peer + distance + quality |
| 3-tag HIL | Multi-peer encounter behavior |
| Anchor profile | Same CS layer; policy-only |
| `tag-prod` | Hublink + Juxta5-8 contracts |

## Non-goals (current waves)

No Hublink GATT, NOR CSV logger, MCUboot/DFU, Encounter Manager, anchor-specific apps, LED2/BTN2, FUEL ADC, or DK Channel Sounding antenna overlays on Tag TWI pins.

## Links

- [Nordic nRF54L15 Tag intro](https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_tag/page/ug/nrf54l15_tag/intro.html)
- [Zephyr board: nRF54L15 TAG](https://docs.zephyrproject.org/latest/boards/nordic/nrf54l15tag/doc/index.html)
- [docs/nRF54L15_Tag_Hardware_Reference.md](docs/nRF54L15_Tag_Hardware_Reference.md)
- [agents/JUXTA-SPEC.md](agents/JUXTA-SPEC.md)
- [lib/juxta_range/README.md](lib/juxta_range/README.md)
- [reference/juxta5-8/REFERENCE.md](reference/juxta5-8/REFERENCE.md)
