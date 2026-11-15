# Juxta6 passive budget (planning / floorplan)

Quick **count estimate** for schematic placeholders and board-area gut-check. Not a final BOM.

**Rules for this sprint**

- Prefer **0201** where able (Tag used many 0201). Use **0402+** only when value/voltage/Q or assembly needs it.
- Bulk / power magnetics: **0603+** or larger (called out).
- No MPNs yet — place generic Device R/C/L with **0201** footprints by default; values as placeholders.
- Counts are **fitted Juxta intent** (photo + W25Q64 + BMI270-only + TPS63900 @ **1.8 V** + PE42421). Tag DNP LED2 / buzzer / ADXL not included.

## Per-function estimate

| Function | R | C | L / FB | Notes (Tag refs where useful) | Size hint |
| --- | ---: | ---: | ---: | --- | --- |
| **Power — TPS63900** | 3 | 3 | 1 | CFG1–3 for **Vout = 1.8 V**; Cin ~10 µF, Cout ~22 µF, optional 100 nF; **L = 2.2 µH** | CFG **0201/0402**; Cin/Cout **0603**; L **≥0805 / 2.0×1.6** |
| **Power — board bulk / filter** | 0 | 1–2 | 0–2 FB | Tag `C39` 47 µF bulk; optional ferrites on VDD branches | Bulk **0603+**; FB **0201** |
| **nRF54L15 SoC decouple + DCDC** | 0–1 | ~15–18 | 1 | Tag: `C1/C2` 2.2 µF×2, `C3` 10 µF, **12×** 100 nF, `C5` 100 pF; **L1** 4.7 µH DCDC | Most **0201**; L1 **0603** |
| **Clocks X1 / X2** | 0 | 2–4 | 0 | Load caps for 32.768 kHz + 32 MHz | **0201** C0G |
| **RF front-end (nRF ↔ PE42421 ↔ A1/A2)** | 0 | ~10–14 | ~3–4 | Tag dual-antenna path (~12 C + 4 L total). Retune with PE42421 @ 1.8 V | RF C/L **0201** High-Q (or 0402 if Q/stock) |
| **PE42421 supply** | 0–1 | 1 | 0 | VDD bypass ~100 nF; optional CTRL weak pull (1.8 V logic) | **0201** |
| **TWI (BME688)** | 2 | 0–2 | 0 | Tag `R2/R3` 10 k pull-ups | **0201** |
| **BMI270 (SPI)** | 0–2 | 2–4 | 0 | Local VDD/IO decouple; optional series R | **0201** |
| **Flash U8 (W25Q64)** | 1 | 1–2 | 0 | Pull-up + local 100 nF (1.8 V part) | **0201** |
| **LED1 RGB** | 3 | 0 | 0 | Recalc series R for **1.8 V** (was Tag 3 V-ish values) | **0201** |
| **BTN1 SW1** | 0–1 | 0–1 | 0 | Optional pull-up / debounce | **0201** |
| **Photo Q1 + Q2/Q3** | 1–3 | 0–1 | 0 | Tag `R4` class; may recalc at 1.8 V | **0201** |
| **SWD testpoints** | 0 | 0 | 0 | Pads only | — |
| **Bat1 / J1** | 0 | 0–1 | 0 | Optional input cap near connector/holder | **0201** / **0603** |

## Rough totals (fitted Juxta)

| Kind | Approx. qty | Comment |
| --- | ---: | --- |
| **Resistors** | **~12–20** | Dominated by LED + CFG + TWI + photo + flash |
| **Capacitors** | **~40–55** | Dominated by SoC decouple + RF match |
| **Inductors** | **~5–7** | 1× TPS 2.2 µH (large), 1× nRF DCDC, ~3–4 RF nH |
| **Ferrite beads** | **0–2** | Optional VDD filter |

**Area gut-check:** treat ~50× **0201** + ~5× **0603** bulk + **1–2 magnetics ≥0805** as the passive floorplan budget, on top of ICs/antennas/connectors already locked.

## Schematic sprint checklist

Hierarchical sheets under `juxta6_v1-0/juxta6_v1-0.kicad_sch` (regenerate via `scripts/populate_schematic_sections.py`):

| Sheet | File | Contents |
| --- | --- | --- |
| Power | `01_power.kicad_sch` | U2 TPS63900 (**1.8 V**), Bat1/J1 DNP, CFG R, Cin/Cout/bulk, L 2µ2, FB×2 |
| MCU_Clocks | `02_mcu_clocks.kicad_sch` | U1, X1/X2, DCDC L, decouple bank, xtal C |
| RF | `03_rf.kicad_sch` | U6 PE42421, A1/A2, RF C/L placeholders, CTRL R |
| Sensors | `04_sensors.kicad_sch` | U3 BME688, U4 BMI270, TWI/SPI R, local C |
| Peripherals | `05_peripherals.kicad_sch` | **U8 W25Q64**, LED1, SW1, Q1–Q3, LED/photo R, C |
| Debug | `06_debug.kicad_sch` | TP1–TP5 (VDD/GND/SWDIO/SWDCLK/RESET) |

Values stay `TBD`/`DNP` until matching and CFG are fixed. No nets yet — floorplan / Update PCB from schematic.

## Related

- Locked specialized parts: [`LCSC_Sourcing_Decision_Aid.md`](LCSC_Sourcing_Decision_Aid.md)
- Tag BOM source: [`../../nrf54l15-tag/nRF54L15_Tag_Hardware_Reference.md`](../../nrf54l15-tag/nRF54L15_Tag_Hardware_Reference.md) § appendix
