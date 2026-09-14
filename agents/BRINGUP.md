# Phase 0 / HIL bring-up checklist (nRF54L15 Tag)

User-owned hardware and nRF Connect steps. Agents do not build, flash, or debug — see [RULES.md](RULES.md).

Architecture: [JUXTA-SPEC.md](JUXTA-SPEC.md).  
Validation log: [docs/HIL_VALIDATION.md](../docs/HIL_VALIDATION.md).

## Checklist

1. **Power the Tag** with exactly one source: CR2032 **or** 3.3 V on DK `VDD SWD0` — **never both**.
2. Insert the Tag into the nRF54L DK **DEBUG OUT** header (DEBUG OUT does not power the Tag).
3. In **nRF Connect**, build/flash with board `nrf54l15tag/nrf54l15/cpuapp` and **RTT Console** (`rtt-console`).
4. Run HIL apps in order; record results in `docs/HIL_VALIDATION.md`.
5. **Channel Sounding** needs **NCS ≥ 3.3.1**. Never apply nRF54L15 DK CS antenna overlays on Tag (those pins are TWI).
6. **Later:** Encounter Manager, flash encounter log, 3-tag HIL, anchor policy, `tag-prod`.

## HIL order

### Wave A — hardware

| # | App | Expect |
| --- | --- | --- |
| 1 | `tag-blink` | LED1 R→G→B→W; BTN1 logs |
| 2 | `tag-btn-magnet` | Hold &lt;3 s reject; 3–10 s green slow; ≥10 s blue fast |
| 3 | `tag-sysoff` | System OFF; BTN1 wakes then re-shelves |
| 4 | `tag-sensors-off` | BMI + BME probe then suspend |
| 5 | `tag-adxl367` | Shake → `motion_count`; BMI/BME off |
| 6 | `tag-flash` | Last-sector PASS (U8 fitted) |
| 7 | `tag-ble-adv` | Adv / connect LED cues |

### Wave B — mobile↔mobile ranging

| # | App | Expect |
| --- | --- | --- |
| 8 | `tag-id` | `juxta_name=JX_…`, `juxta_profile=mobile` |
| 9 | `tag-discover` | Two identical images; mutual `peer_seen` / `peer_lost` |
| 10 | `tag-cs` | Lower ID initiates (green) / higher reflects (blue); RTT distance; BTN1 forces role |

## Hardware notes

| Topic | Detail |
| --- | --- |
| Board | `nrf54l15tag/nrf54l15/cpuapp` |
| NCS | v3.3.1 or newer |
| Magnet stand-in | **BTN1** (`sw0`) |
| LED | **LED1** RGB only |
| Motion | **ADXL367** only |
| CS API | Only via `lib/juxta_range` |
| Anchors | Same image later — policy only; not optimized in this wave |

## West equivalent (reference only)

```bash
west build -b nrf54l15tag/nrf54l15/cpuapp --snippet rtt-console applications/tag-cs
west flash
```
