# Juxta6 LCSC / JLCPCB sourcing decision aid

Specialized (non-passive) parts for `juxta6_v1-0`, derived from the Nordic nRF54L15 Tag (PCA20072) contract and `lcsc_manifest.csv`.

**Juxta vs stock Tag:** phototransistor (Q1/Q2/Q3 + R4) and flash (U8) are **fitted**. **No ADXL367** — motion is **BMI270 only**. Power uses **TPS63900** with **CR2032 XOR 2 mm JST LiPo**, **Vout locked to 1.8 V**. Coin-holder and LiPo connector footprints both on the PCB; **default DNP both**, then DigiKey hand-place whichever source that build uses (never energize both).

**Passives** (R/C/L/FB, RF matching, photo/memory, PMIC surrounding) are **not** imported via easyeda2kicad — KiCad Device + JLCPCB Basic / TI EVM BOM. Prefer **0201** where able; use **0402+** for bulk / power magnetics / RF High-Q as needed.

Stock figures are JLCPCB snapshots (refresh with `scripts/stock_report.py`).

| Ref | Function (in this circuit) | Tag / project MPN | Package | LCSC | LCSC MPN | JLC stock | Lib status | Decision notes |
| --- | --- | --- | --- | --- | ---: | --- | --- | --- |
| U1 | Wireless SoC: BLE / 802.15.4 / CS radio, CPU, GPIO, buses | nRF54L15-QFAA-C00 | QFN-48 | C42458750 | NRF54L15-QFAA-R | **0** | imported | DigiKey / Mouser / Nordic supply |
| U3 | Environmental sense on TWI (`0x76`) | BME688 | LGA-8 | C3664478 | BME688 | **4480** | imported | Prefer JLC SMT; OK at 1.8 V |
| U4 | **Sole** motion IMU: low-power impulse wake + optional 6-axis record (SPI) | BMI270 | LGA-14 | C2836813 | BMI270 | **5255** | **locked** | Replaces Tag ADXL367+BMI270 dual-sensor setup; OK at 1.8 V |
| U5 | *(Tag only — not on Juxta6)* | ADXL367BCCZ | LGA-12 | C17643415 | ADXL367BCCZ-RL7 | **2** | **not used** | Leave off schematic; symbol may remain in lib |
| U6 | SPDT RF switch A1/A2 (CS antenna select) | **PE42421SCAA-Z** | SC-70-6 | **C470893** | PE42421SCAA-Z | **606** | **locked** | Replaces Tag SKY13348. **1.8 V capable** (at VDD min — expect reduced P1dB). Not a drop-in vs SKY pinout |
| U6 Tag | *(Tag only — not on Juxta6)* | SKY13348-374LF | XFDFN-6 | C150883 | SKY13348-374LF | **0** | **not used** | Discontinued; leave off schematic |
| U6 alt | *(Ruled out for 1.8 V)* | AS179-92LF | SC-70-6 | — | AS179-92LF | — | **not used** | Control **Vhigh** typically **≥2 V** (0/+3 V class) — not compatible with 1.8 V GPIO/rail without level shift |
| U8 | SPI NOR logs/data (CS `P2.05`) | **W25Q64JWUUIQ** | USON-8 (4×3) | **C6604692** | W25Q64JWUUIQ | **~14.7k** | **locked** | **1.7–1.95 V** 64 Mbit / 8 MiB. Replaces MX25L3233 (2.65 V min — incompatible with 1.8 V VDD) |
| U8 old | *(Superseded)* | MX25L3233FZBI-08G-TR | USON-8 (4×3) | C2803001 | MX25L3233FZBI-08GTR | **187** | **not used** | 3 V-class flash; keep symbol in lib only |
| U2 | Buck-boost: selected source → **VDD = 1.8 V** | TPS63900DSKR | WSON-10-EP 2.5×2.5 | C1518762 | TPS63900DSKR | **5447** | **locked** | See [power](#power-architecture-juxta) |
| Bat1 | CR2032 holder → PMIC (optional / **default DNP**) | **BU2032SM-FH-GTR** | SMD coin 20 mm | C6755625 | BU2032SM-FH-GTR | **0** | **imported** DigiKey CAD | Primary land pattern: `juxta6:BU2032SM-FH-GTR` / `BU2032SM-FH-GTR_MPD`. Fit only for coin builds |
| Bat1 alt | Same role — JLC SMT option | BS-06-B4AA012 | SMD | **C964758** | BS-06-B4AA012 | **5202** | imported alt | Different FP than BU2032 — choose one holder for the layout |
| J1 | LiPo PH 2.0 RA → PMIC (**default DNP**) | **S2B-PH-SM4-TB(LF)(SN)** | SMD RA 2 mm | **C295747** | S2B-PH-SM4-TB(LF)(SN) | **~115** | **locked** / DNP default | DigiKey hand-place for LiPo builds |
| Q1 | Ambient light (550 nm phototransistor) | TEMT6200FX01 | 0805 | C143695 | TEMT6200FX01 | **2228** | imported | Exact Tag MPN |
| Q2, Q3 | N-FETs in phototransistor path | RV2C010UNT2L | X2-DFN1006-3 | C570828 | RV2C010UNT2L | **506** | imported | Place ×2 |
| A1, A2 | Dual 2.45 GHz CS antennas via PE42421 (identical pair) | **2450AT18D0100001E** | SMD-4P ~3.2×1.6 | **C7450310** | 2450AT18D0100001E | **267** | **locked** | Tag-family Johanson; qty **2** |
| LED1 | RGB status | APHF1608LSEEQBDZGKC | 1608 | C3019171 | APHF1608LSEEQBDZGKC | **3905** | imported | Recalc series R for **1.8 V** rail |
| X1 | LFCLK 32.768 kHz, **9 pF** | **X1A0000610008** (Epson) | SMD2012-2P | **C96341** | X1A0000610008 | **~18.5k** | **locked** | |
| X2 | HFCLK 32 MHz, **CL=8 pF** | **X201632MKB4SI** (YXC) | SMD2016-4P | **C718072** | X201632MKB4SI | **~85k** | **locked** | |
| SW1 | BTN1 (`P0.00`) | **3.2X4.2X3.0DH260g-QBPJ** | SMD-4P ~4.2×3.4 | **C55232901** | 3.2X4.2X3.0DH260g-QBPJ | **1727** | **locked** | |
| TP×5 | Program / SWD bring-up (no connector) | custom 5-pad | testpoints | — | — | — | **planned** | Pads: **VDD, GND, SWDIO, SWDCLK, RESET**. Pitch TBD |

### Related passives (Device libs)

| Ref | Role | Notes |
| --- | --- | --- |
| R4 | Photo divider | Recalc for 1.8 V if needed |
| R17 / C38 | Flash pull-up / decouple | Fitted with W25Q64 |
| L/C around TPS63900 | Buck-boost; CFG for **1.8 V** Vout | Per TI datasheet / EVM |
| Most R/C/RF L | Floorplan | Prefer **0201**; bulk/power **0402–0603+** |

## Power architecture (Juxta)

Departs from Tag “CR2032 direct to VDD, no regulator.”

**Policy: coin XOR LiPo** — footprints for both may be on the board; only one source populated/energized.

**Default assembly:** **Bat1 = DNP**, **J1 = DNP**. Hand-place from DigiKey per build: coin holder **or** `S2B-PH-SM4-TB` LiPo connector — not both.

**Locked Vout: 1.8 V** (power-savings topology). TPS63900 CFG resistors set this. Single rail feeds SoC, sensors, flash, PE42421.

```text
  CR2032 (Bat1)     ──►┐   default DNP; DigiKey hand-place for coin builds
                       ├──► TPS63900 ─► VDD = 1.8 V
  LiPo (J1 C295747) ──►┘   default DNP; DigiKey hand-place for LiPo builds
```

Open choices (ask before inventing):

1. **Which coin-holder FP** to lay out: DigiKey **BU2032SM-FH-GTR** (preferred CAD) vs JLC **C964758** / BS-06.
2. PE42421 CTRL wiring at 1.8 V logic (single-pin vs complementary).

## Motion sensor decision

**Locked:** **BMI270 only** (SPI). Do **not** place ADXL367 on Juxta6.

## U8 flash decision

**Locked:** `W25Q64JWUUIQ` / **C6604692** — **1.8 V** 64 Mbit USON-8. Symbol `juxta6:W25Q64JWUUIQ`, FP `USON-8_L4.0-W3.0-P0.80-BL_WINBOND_UU`.

**Not used:** MX25L3233 (VCC min 2.65 V — fails 1.8 V rail).

## RF switch decision

**Locked:** **PE42421** (operates at 1.8 V VDD min).

**Not used:** Tag SKY13348 (EOL); **AS179-92LF** (needs ~3 V control high — out for 1.8 V topology).

## RF antennas + crystals

**Antennas (×2):** Johanson **C7450310**. **X1:** Epson **C96341**. **X2:** YXC **C718072**.

## Suggested decision buckets

| Bucket | Refs | Likely path |
| --- | --- | --- |
| JLC SMT OK | U3, U4, U6 PE42421, **U8 W25Q64**, U2 TPS63900, Q1, Q2/Q3, **A1/A2**, LED1, SW1, **X1**, **X2** | Current LCSC IDs |
| DigiKey hand-place / DNP | **Bat1**, **J1** (C295747); also U1 | CAD on board; stuff one power source per build |
| Need human pick | Coin-holder FP; SWD TP pitch; PE42421 CTRL at 1.8 V | Before schematic freeze |
| Not on Juxta6 | U5 ADXL367; SKY13348; AS179-92LF; MX25L3233 | Do not place |
| Not imported | R/C/L/FB + TPS63900 passives | Device + TI EVM; prefer **0201** |

## Related files

- Passive counts (0201 preferred): [`Passive_Budget_Estimate.md`](Passive_Budget_Estimate.md)
- Manifest: [`lcsc_manifest.csv`](lcsc_manifest.csv)
- Tag contract: [`../../nrf54l15-tag/nRF54L15_Tag_Hardware_Reference.md`](../../nrf54l15-tag/nRF54L15_Tag_Hardware_Reference.md)
- Agent rules: [`../AGENTS.md`](../AGENTS.md)
- Refresh stock: `../scripts/stock_report.py`
- Search alts: `../scripts/search_jlc.py <keyword>`
