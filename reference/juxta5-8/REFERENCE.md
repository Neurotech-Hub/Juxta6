# Juxta5-8 reference snapshot

Frozen behavioral / protocol reference for porting to a new platform (e.g. nRF54L15 Tag + ADXL367). **Do not west-build this tree** as part of the new project.

## Included

| Path | Role |
| --- | --- |
| `README.md` | Repo overview, Juxta5-8 pin map, fuel divider, bring-up plan |
| `docs/JUXTA_NOR_Flash_Logging_Spec_v3.md` | NOR CSV logging contract (prod live schema is `jxta-nor-csv-v5`) |
| `applications/juxta5-8-prod/` | Production Hublink firmware: GATT, state machine, NOR logging, recovery, DFU |
| `applications/juxta5-8-blink/` | LED / magnet / FUEL ADC bring-up pattern |
| `applications/juxta5-8-ble-test/` | Connectable GATT + magnet System OFF pattern |
| `applications/juxta5-8-ble-range/` | Minimal `JX_XXXXXX` adv/scan peer pattern |
| `applications/juxta5-8-mem/` | External SPI NOR erase/write/verify pattern |
| `boards/NeurotechHub/Juxta5-8_nRF52840/` | Historical board DTS / pins / TX power |

## Excluded on purpose

- **LIS2DH12** sources and `juxta5-8-axy` — new hardware uses ADXL367
- **Crash analysis** notes — hardware/revision-specific
- **Build artifacts** (`build/`, DFU zips)
- Ancestral `Reference/nRF` FRAM / older boards

## Primary contracts to reimplement against

1. `applications/juxta5-8-prod/README.md` — Hublink JSON, magnet/shelf/DFU semantics, JXS lifecycle
2. `docs/JUXTA_NOR_Flash_Logging_Spec_v3.md` + prod `juxta_log` / `juxta_prod.h` — offload schema
3. Board DTS — map equivalents on the new Tag-derived hardware (wake, LED, storage, fuel)
