# nRF54L15 Tag — Hardware Reference for Firmware Development & Tests

> **Scope:** PCA20072, hardware/BOM v1.0.0. This is a firmware-facing hardware contract distilled from the Nordic **nRF54L15 Tag Hardware User Guide v1.0.0** and the attached **PCA20072 BOM v1.0.0**.
>
> **Project-specific override:** The stock BOM marks **U8 / MX25R6435FZBIH3** as *Not Fitted*. For this project, **assume U8 is populated before use**. Firmware and tests should therefore treat the external flash as required hardware unless explicitly running against an unmodified stock Nordic tag.

## 1. Agent quick reference

```yaml
board:
  product: nRF54L15 Tag
  pca: PCA20072
  hardware_revision: 1.0.0
  guide_version: 1.0.0
  guide_date: 2026-06-16
  guide_valid_for_hw_revisions: [0.7.0, 1.0.0]

soc:
  part: nRF54L15-QFAA-C00
  package: QFN-48

project_required_population:
  U8:
    part: MX25R6435FZBIH3
    function: 64-Mbit serial multi-I/O flash
    logical_capacity: 8_MiB
    interface: QSPI
    stock_bom_status: Not Fitted
    project_status: Fitted before use

motion_sensors:
  low_power_accelerometer:
    part: ADXL367BCCZ
    axes: 3
    ranges_g: [2, 4, 8]
    board_interface: TWI/I2C-compatible
    address: 0x1D
  imu:
    part: BMI270
    axes: 6
    composition: 3-axis accelerometer + 3-axis gyroscope
    board_interface: dedicated SPI

environmental_sensor:
  part: BME688
  measures: [gas, pressure, temperature, humidity]
  board_interface: TWI/I2C-compatible
  address: 0x76

user_io:
  BTN1: P0.00
  LED1:
    red: P2.08
    green: P2.10
    blue: P2.09

rf:
  antennas: 2
  antenna_part: 2450AT18D0100
  band: 2.4_GHz
  switch: SKY13348-374LF
  purpose: Bluetooth Channel Sounding antenna switching

power:
  default_source: CR2032
  regulation: none_dedicated
  battery_and_sensors: direct_supply
  current_measurement_bridge: SB1
```

### Critical implementation guardrails

- **There are two separate inertial sensors.** `ADXL367` is the **3-axis low-power accelerometer**. `BMI270` is the **6-axis IMU**. Never model the ADXL367 as the 6-axis device.
- The BMI270's “6-axis” means **3-axis accelerometer + 3-axis gyroscope**; no magnetometer is listed on this board.
- `BME688` and `ADXL367` share the same TWI/I²C-compatible bus but have different addresses (`0x76` and `0x1D`).
- `BMI270` is wired through a **dedicated SPI interface**, not the shared TWI bus.
- The two 2.4 GHz antennas are connected through a **single SPDT RF switch** to the nRF54L15 RF port. They are selectable paths, not two simultaneous RF chains.
- The stock Nordic tag does **not** populate U8. **This project does.** Treat `P2.05` as occupied by QSPI chip-select in the project hardware configuration.
- Do not assume optional footprints are populated simply because firmware support exists.

## 2. Board-level capabilities

The tag is a compact nRF54L15 firmware-development platform with:

- nRF54L15 SoC in QFN48 package.
- Bluetooth Low Energy.
- IEEE 802.15.4.
- Matter.
- Thread.
- Zigbee.
- ANT.
- 2.4 GHz proprietary radio.
- Dual 2.4 GHz antennas intended to support Bluetooth Channel Sounding.
- Low-power 3-axis accelerometer (`ADXL367`).
- 6-axis IMU (`BMI270`).
- Environmental sensor (`BME688`).
- One populated user button.
- One populated RGB LED.
- Footprints for an additional button, reset switch, RGB LED, buzzer, phototransistor circuitry, and external serial flash.
- CR2032 coin-cell holder.
- SWD programming/debugging through an external Nordic DK or SEGGER debugger.
- OTA device firmware update support.

**Source:** Nordic User Guide §§1–3, especially pp. 6, 9–18.

## 3. Effective hardware population

### Required / populated for this project

| Ref | Part | Function | Effective status |
|---|---|---|---|
| U1 | nRF54L15-QFAA-C00 | Wireless MCU / SoC | Fitted |
| U3 | BME688 | Gas, pressure, temperature, humidity sensor | Fitted |
| U4 | BMI270 | 6-axis IMU | Fitted |
| U5 | ADXL367BCCZ | Micropower 3-axis accelerometer | Fitted |
| U6 | SKY13348-374LF | 50 MHz–6 GHz SPDT RF switch | Fitted |
| U8 | MX25R6435FZBIH3 | 64-Mbit serial multi-I/O flash | **Fitted by project assumption** |
| A1, A2 | 2450AT18D0100 | 2.45 GHz chip antennas | Fitted |
| LED1 | APHF1608LSEEQBDZGKC | RGB LED | Fitted |
| SW1 / BTN1 | L-KLS7-TS3402-2.5-250-B-T | User pushbutton | Fitted |
| Bat1 | L-KLS5-CR2032-23-BR | CR2032 holder | Fitted |
| P1 | 2344-205RS0CUNR3 | 2×5 1.27 mm right-angle debug socket | Fitted |
| X1 | 9HT11-32.768KDZC-T | 32.768 kHz crystal, 9 pF, ±20 ppm | Fitted |
| X2 | CX2016DB32000D0WZRC1 | 32 MHz crystal, CL=8 pF, total ±40 ppm | Fitted |

### Optional footprints not populated in the stock BOM

| Ref | Part / footprint | Intended capability | Default/project assumption |
|---|---|---|---|
| BTN2 / SW2 | KMT021 NGJ LHS | Second user button | Not fitted |
| RST / SW3 | KMT021 NGJ LHS | Physical reset button | Not fitted |
| LED2 | APHF1608LSEEQBDZGKC | Second RGB LED | Not fitted |
| Bz1 | PKMCS0909E4000-R1 | 4 kHz piezo buzzer, 65 dB @ 1.5 V | Not fitted |
| Q1 | TEMT6200FX01 | 550 nm top-view phototransistor | Not fitted |
| Q2, Q3 | RV2C010UNT2L | N-channel MOSFETs, 20 V / 1 A | Not fitted |
| P2 | 1×3, 2.54 mm header footprint | External power/header access | Header not fitted; pads remain usable |
| U8 | MX25R6435FZBIH3 | External serial flash | **Stock: not fitted; project: fitted** |

Do not enable or test optional peripherals by default unless the particular assembled hardware variant is known to populate them.

## 4. Sensors and buses

### 4.1 BME688 environmental sensor

- **Reference:** U3
- **Manufacturer:** Bosch
- **Part:** BME688
- **Board function:** gas, pressure, temperature, and humidity sensing.
- **Board bus:** shared TWI/I²C-compatible bus.
- **TWI address:** `0x76`.
- **IRQ:** none documented in the board user guide.

Firmware contract:

- Probe/address tests should expect `0x76` on the shared TWI bus.
- Do not describe this device as a dedicated CO₂ sensor; the board documentation only specifies gas concentration plus pressure/humidity/temperature.

### 4.2 ADXL367 low-power accelerometer

- **Reference:** U5
- **Manufacturer:** Analog Devices
- **Part:** ADXL367BCCZ
- **Axes:** 3-axis accelerometer only.
- **Ranges stated in BOM:** ±2 g / ±4 g / ±8 g.
- **Board bus:** shared TWI/I²C-compatible bus.
- **TWI address:** `0x1D`.
- **Interrupt net shown:** `ADXL_IRQ`.
- **Exact nRF GPIO for `ADXL_IRQ`:** not given in this hardware user guide.

Recommended role: always-on or low-duty-cycle motion/activity sensing and wake qualification, with the BMI270 reserved for full accel+gyro measurements when needed.

### 4.3 BMI270 6-axis IMU

- **Reference:** U4
- **Manufacturer:** Bosch
- **Part:** BMI270
- **Axes:** 6 total = 3-axis accelerometer + 3-axis gyroscope.
- **Board bus:** dedicated SPI.
- **Signals shown:** MISO, MOSI, SCK, CS, `BMI_IRQ`.
- **Exact nRF GPIO mapping:** not enumerated in the hardware user guide; use the Nordic board DTS/schematic rather than guessing.

### 4.4 Shared TWI bus

The BME688 and ADXL367 share the same bus. The guide identifies test-point GPIOs `P1.11` and `P1.12` as TWI pins, but does **not** explicitly state in the table which is SDA vs. SCL. Do not hard-code that assignment from this document alone.

BOM implementation notes:

- `R2`, `R3`: 10 kΩ resistors associated with the TWI lines.
- `R5`: 10 Ω series resistor shown in the sensor circuit.

## 5. External flash — project-required hardware

### MX25R6435F

- **Reference:** U8.
- **Manufacturer:** Macronix.
- **Exact BOM part:** `MX25R6435FZBIH3`.
- **Package:** USON-8.
- **Description:** ultra-low-power 64-Mbit serial multi-I/O flash.
- **Capacity:** 64 Mbit = **8 MiB**.
- **Board interface:** QSPI / serial multi-I/O.
- **Chip select:** `P2.05` is documented as the QSPI CS GPIO when flash is populated.
- **Stock Nordic BOM:** Not Fitted.
- **Project hardware contract:** **Fitted before use.**

Firmware/test implications:

1. Treat external flash initialization as a normal boot-path capability for the project tag.
2. Do not expose `P2.05` as a free GPIO in the project board definition.
3. Include a flash presence/identity test in hardware-in-loop diagnostics.
4. Prefer a non-destructive capacity/read test during ordinary boot diagnostics.
5. If erase/write verification is needed, reserve a dedicated test sector/partition so a factory test cannot destroy application data.
6. The board guide does not state the complete QSPI pin mapping or JEDEC-ID constants; obtain those from the Nordic board definition/schematic and Macronix datasheet rather than inferring them here.

## 6. User input and LEDs

### Buttons

| Function | GPIO | Population |
|---|---:|---|
| BTN1 | `P0.00` | Fitted |
| BTN2 | `P0.01` | Footprint only / not fitted |
| RST | Reset line, not a GPIO mapping | Footprint only / not fitted |

The stock board has **one actual user button**. A developer should not require BTN2 or a physical reset switch for normal operation.

### RGB LEDs

| LED | Channel | GPIO | Population |
|---|---|---:|---|
| LED1 | Red | `P2.08` | Fitted |
| LED1 | Green | `P2.10` | Fitted |
| LED1 | Blue | `P2.09` | Fitted |
| LED2 | Red | `P0.02` | Not fitted |
| LED2 | Green | `P0.04` | Not fitted |
| LED2 | Blue | `P2.07` | Not fitted |

LED1 current-limiting resistors in the BOM are `R11=390 Ω`, `R12=2.2 kΩ`, `R13=1.2 kΩ`. LED2 has corresponding unpopulated `R14/R15/R16`.

The user guide does not explicitly define logical LED polarity in text. Use the Nordic board definition/schematic before encoding active-high vs. active-low behavior in low-level tests.

## 7. RF and Bluetooth Channel Sounding hardware

### RF components

- Two identical 2.4 GHz chip antennas:
  - `A1`: Johanson `2450AT18D0100`
  - `A2`: Johanson `2450AT18D0100`
- RF switch:
  - `U6`: Skyworks `SKY13348-374LF`
  - SPDT, specified in BOM as 50 MHz–6.0 GHz.
- nRF54L15 has one RF path connected to one of the two antennas through U6.
- Control net is shown as `ANTSEL`.
- The exact nRF GPIO used for `ANTSEL` is not enumerated in the guide.

### Channel Sounding behavior

Nordic states that the two antennas can be used with Bluetooth Channel Sounding. When the **nRF Connect SDK Channel Sounding library** is enabled, antenna switching is automatic.

**Important electrical constraint:** If RF-switch pins V1 and V2 are both pulled low, the switch's insertion-loss state is undefined. Firmware or board bring-up code should avoid any transient configuration that intentionally drives the RF switch into that state.

### RF BOM/network notes

The RF implementation includes fitted matching/coupling components including `C25/C28/C29=47 pF`, `L7=2.7 nH`, `C30=1.2 pF`, `C40=6.8 pF`, `C41=2.0 pF`, `C42=1.5 pF`, `C26=0.8 pF`, with `C27` and `C31` not mounted. Preserve Nordic's RF layout and matching network unless deliberately re-tuning the antenna system.

## 8. Power architecture

- Primary intended supply: **CR2032 coin cell**.
- The CR2032 powers the nRF54L15 and sensors directly.
- Nordic explicitly states that this eliminates the need for dedicated power-management circuitry.
- External power can be applied via the P2 footprint or test points on the back of the board.
- The board exposes `VBAT`, `VDD`, and `GND` test points.
- `SB1` connects `VBAT` to `VDD` and is **closed by default**.
- `C39` is a fitted 47 µF bulk capacitor.

### Safety / development constraint

**Never apply external power while a battery is installed.** Nordic warns that doing so can overheat and permanently damage the tag, DK, and battery.

For DK-powered examples and current measurement, the guide uses **3.0 V**.

External supply guidance in the user guide: PS1 class (IEC 62368-1), maximum power <15 W.

Operating environment specified by Nordic: **0 °C to +40 °C**. The board is ESD-sensitive.

## 9. Current measurement support

Supported external instruments listed by Nordic:

- Nordic Power Profiler Kit II (PPK2).
- Oscilloscope.
- Ampere meter.
- Power analyzer.

To measure average board current with a meter:

1. Cut `SB1` to disconnect `VBAT` from `VDD`.
2. Power by CR2032 **or** 3.0 V external supply, never both.
3. Insert the meter in series between the exposed measurement points specified by Nordic.
4. Use long averaging (Nordic suggests ≥1 s for an ampere meter).
5. Instrument dynamic range should cover roughly **1 µA to 15 mA**.

For automated power regression tests, prefer PPK2 or a power analyzer over a conventional meter so short radio/sensor bursts are not hidden by long averaging.

## 10. Exposed GPIO / test-point contract

The user guide lists the following exposed GPIOs:

| GPIO | Default board use | Project interpretation |
|---|---|---|
| `P0.01` | BTN2 footprint | Free only if BTN2 remains unpopulated |
| `P0.02` | LED2 red footprint | Free only if LED2 remains unpopulated |
| `P0.04` | LED2 green footprint | Free only if LED2 remains unpopulated |
| `P1.02` | None | Free |
| `P1.03` | None | Free |
| `P1.11` | TWI | **Reserved** |
| `P1.12` | TWI | **Reserved** |
| `P1.13` | None | Free |
| `P1.14` | None | Free |
| `P2.05` | QSPI CS when external flash is mounted | **Reserved in this project** |
| `P2.06` | None | Free |
| `P2.07` | LED2 blue footprint | Free only if LED2 remains unpopulated |

Also exposed: `VBAT`, `VDD`, `GND`.

Some test points are through-hole pads to allow mechanically stronger soldered cable attachment.

## 11. Programming and debugging

### Supported external programmers/debuggers

- nRF54L Series DK.
- nRF53 Series DK.
- nRF91 Series DK.
- SEGGER J-Link / debugger.

For nRF53/nRF91 DKs, ensure debugger I/O voltage matches the tag.

### P1 debug header pinout

| Pin | Signal | Meaning |
|---:|---|---|
| 1 | `VDD_DBG` | I/O voltage from tag; same as nRF VDD / battery voltage |
| 2 | `SWDIO` | SWD data |
| 3 | `SELECT` | Tag-detect signal; tied to GND on tag |
| 4 | `SWCLK` | SWD clock |
| 5 | `GND` | Ground |
| 6 | `SWO` | Trace output; not required for SWD programming/debugging |
| 7 | N.C. | Not used |
| 8 | N.C. | Not used |
| 9 | N.C. | Not used |
| 10 | `RESET` | Reset |

The populated BOM connector is a 2×5, 1.27 mm right-angle through-hole socket.

OTA-DFU is supported by the platform and should be treated separately from physical SWD recovery/programming.

## 12. Clock hardware

The BOM includes:

- `X1`: 32.768 kHz crystal, 9 pF load, ±20 ppm.
- `X2`: 32 MHz crystal, CL=8 pF, total ±40 ppm.

No separate RTC IC is listed in the BOM. Do not invent an external RTC dependency in firmware architecture.

## 13. Optional hardware footprints worth preserving in software architecture

The BOM exposes several future expansion possibilities that are not part of the default runtime contract:

- Second RGB status LED.
- Second user pushbutton.
- Physical reset switch.
- 4 kHz SMD piezo buzzer.
- 550 nm phototransistor.
- Two small N-channel MOSFET footprints.
- External power header.

Where practical, keep these behind feature flags/devicetree status rather than hard-coding assumptions that they exist.

## 14. Suggested firmware/HIL test matrix

### 14.1 Boot smoke test

Expected on the **project-populated** tag:

- nRF54L15 boots from reset.
- TWI controller initializes.
- BME688 responds at `0x76`.
- ADXL367 responds at `0x1D`.
- BMI270 responds on its dedicated SPI bus.
- MX25R6435F responds on QSPI.
- LED1 can be controlled on all three channels.
- BTN1 can be sampled.

### 14.2 Sensor driver tests

**BME688**
- Bus/address probe.
- Chip identity/status read using the Bosch driver.
- One-shot or normal measurement smoke test.
- Validate that returned pressure/temperature/humidity/gas fields are non-error values; do not impose unrealistic environmental bounds in unit tests.

**ADXL367**
- Bus/address probe.
- Chip identity read.
- Configure one supported acceleration range.
- Read XYZ acceleration.
- Interrupt path test only once the exact `ADXL_IRQ` GPIO is taken from the board DTS/schematic.

**BMI270**
- SPI transaction/chip identity test.
- Initialize accelerometer and gyroscope.
- Confirm both 3-axis vectors can be read.
- Interrupt path test only once the exact `BMI_IRQ` GPIO is sourced from the board DTS/schematic.

### 14.3 Flash tests

- Presence/JEDEC identity.
- Capacity expectation: 64 Mbit / 8 MiB.
- Read known erased region.
- Dedicated destructive test partition: erase → write pattern → readback → restore/erase.
- Do not run whole-chip erase in normal CI/HIL tests.

### 14.4 GPIO tests

- BTN1 on `P0.00`.
- LED1 channels on `P2.08`, `P2.10`, `P2.09`.
- Free test-point GPIO loopback tests may use `P1.02`, `P1.03`, `P1.13`, `P1.14`, `P2.06` if fixtures allow.
- Do not use `P1.11/P1.12` (TWI) or `P2.05` (project QSPI CS) as general test GPIOs.

### 14.5 RF / Channel Sounding tests

- BLE advertising/connection smoke test.
- Channel Sounding initialization using the nRF Connect SDK library.
- Verify the SDK-controlled antenna switching path executes without errors.
- If a fixture supports RF measurements, test both antenna paths individually.
- Do not infer a dual-radio/MIMO architecture from the presence of two antennas.

### 14.6 Power tests

Suggested regression states:

- Deep idle / system-off baseline.
- ADXL367-only low-power monitoring.
- BME688 measurement burst.
- BMI270 active sampling.
- QSPI flash read/write burst.
- BLE advertising.
- BLE connected idle.
- Channel Sounding operation.

Measure with PPK2/power analyzer and define acceptance bands only after capturing known-good hardware because the user guide does not provide board-current targets for these application states.

## 15. Things the development agent must **not infer** from this document

The hardware guide/BOM do **not** provide enough information to safely hard-code the following:

- Exact nRF GPIOs for BMI270 SPI/IRQ.
- Exact nRF GPIO for `ADXL_IRQ`.
- Exact nRF GPIO for `ANTSEL`.
- Complete QSPI signal mapping for U8 beyond documented `P2.05` CS usage.
- TWI SDA-vs-SCL assignment between `P1.11` and `P1.12`.
- LED electrical polarity/active-high vs. active-low semantics.
- Sensor chip-ID values, register maps, ODRs, FIFOs, interrupt semantics, noise, or current consumption.
- MX25R6435F JEDEC ID, erase geometry, command set, or deep-power-down behavior.
- nRF54L15 CPU/RAM/internal-flash specifications.

For those, use the **Nordic board DTS/schematic/layout files** and the relevant manufacturer datasheets. Do not substitute guesses into unit tests.

## 16. Source documents

- **Nordic Semiconductor:** *nRF54L15 Tag Hardware User Guide v1.0.0*, document 4557_015, dated 2026-06-16. Valid for tag HW revisions v0.7.0 and v1.0.0.
- Nordic online guide: <https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_tag/page/ug/nrf54l15_tag/intro.html>
- **Attached BOM:** PCA20072, v1.0.0, product `nRF54L15 Tag`, creation date 2026-04-13.
- Project override supplied with this reference: populate `U8 / MX25R6435FZBIH3` before use.

---

## Appendix A — Full attached BOM

This appendix preserves the attached BOM in searchable text. `Effective status` differs from the BOM only for U8, where the project requires the flash to be populated.

| Designator | Value | Description | Footprint | Manufacturer | Manufacturer part | BOM status | BOM qty | Effective status |
|---|---|---|---|---|---|---|---:|---|
| A1, A2 | 2450AT18D0100 | 2.45 GHz chip antenna | 2450AT18D0100 | Johanson Technology | 2450AT18D0100 | Fitted | 2 | Fitted |
| Bat1 | Bat Holder CR2032 | Bat Holder CR2032 SMD | CR2032_HOLDER | KLS Electronic | L-KLS5-CR2032-23-BR | Fitted | 1 | Fitted |
| Bz1 | Piezo_4kHz | Piezo Buzzer, SMD, 9.0mm x 9.0mm x 1.9mm, 4kHz, 65dB @ 1.5V, <12.5Vp-p | Buzzer_PKMCS0909 | Murata | PKMCS0909E4000-R1 | Not Fitted | 0 | Not Fitted |
| C1, C2 | 2.2µF | Capacitor, Ceramic, 2.2µF 2.5V X6T 0201,±20% | 0201 | Murata | GRM033D80E225ME47D | Fitted | 2 | Fitted |
| C3 | 10µF | Capacitor, X5R, ±20%, 6.3V | 0402 | Murata | GRM155R60J106ME05D | Fitted | 1 | Fitted |
| C4, C7, C8, C10, C15, C16, C18, C20, C21, C22, C23, C24 | 100nF | Capacitor, X5R, ±10%, 100nF, 16V | 0201 | N.A. | N.A. | Fitted | 12 | Fitted |
| C5 | 100pF | Capacitor, NP0, ±2%, 25V | 0201 | Murata | GRM0335C1E101GA01D | Fitted | 1 | Fitted |
| C6 | 1.5pF | Capacitor, NP0, ±0.05pF, 25V, High Q | 0201 | Murata | GJM0335C1E1R5WB01 | Fitted | 1 | Fitted |
| C9 | 2.0pF | Capacitor, NP0, ±0.05pF, 25V | 0201 | Murata | GJM0335C1E2R0WB01 | Fitted | 1 | Fitted |
| C11 | 0.3pF | Capacitor, C0G, ±0.1pF, 50V | 0201 | Murata | GRM0335C1HR30BA01 | Fitted | 1 | Fitted |
| C12 | 18pF | Capacitor, C0G, ±5%, 25V | 0201 | Walsin Technology Corporation | 0201N180J250CT | Fitted | 1 | Fitted |
| C13 | 3.9pF | Chip Cap 0201 | 0201 | N.A. | N.A. | Fitted | 1 | Fitted |
| C17, C19 | TBD | Chip Cap 0402 | 0402 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| C25, C28, C29 | 47pF | Capacitor, High Q, Low Loss, C0G/NP0, 25V, ±2%, -55°C ~ 125°C | 0201 | Johanson Technology | QLCD250Q470G1GV001T | Fitted | 3 | Fitted |
| C26 | 0.8pF | Capacitor, NP0, ±5% | 0402 | Murata | GRM1555C1HR80WA01D | Fitted | 1 | Fitted |
| C27, C31 | N.C. | Not mounted | 0402 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| C30 | 1.2pF | Capacitor, NP0, ±0.1pF | 0402 | N.A. | N.A. | Fitted | 1 | Fitted |
| C38 | 100nF | Capacitor, X5R, ±10%, 100nF, 16V | 0201 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| C39 | 47µF | Capacitor, X5R, ±20%, 6.3V | 0603 | N.A. | N.A. | Fitted | 1 | Fitted |
| C40 | 6.8pF | Capacitor, High Q, Low Loss, C0G/NP0, 50V, ±0.1pF, -55°C ~ 125°C | 0402 | Murata | GJM1555C1H6R8BB01D | Fitted | 1 | Fitted |
| C41 | 2.0pF | Capacitor, High Q, Low Loss, C0G/NP0, 50V, ±0.1pF, -55°C ~ 125°C | 0402 | Murata | GJM1555C1H2R0BB01 | Fitted | 1 | Fitted |
| C42 | 1.5pF | Capacitor, High Q, Low Loss, C0G/NP0, 50V, ±0.1pF, -55°C ~ 125°C | 0402 | Murata | GJM1555C1H1R5BB01D | Fitted | 1 | Fitted |
| D1 | BZB984-C12,115 | Zener Diode Array, 1 Pair, Common Anode, 12V, 265mW, ±5% | SOT-663 | Nexperia | BZB984-C12,115 | Not Fitted | 0 | Not Fitted |
| FB1, FB2 | 120R/0.2A | Ferrite Bead, 120 Ohm @ 100MHz, 200mA, 500 mOhm Max | 0201 | Murata | BLM03AG121SN1D | Fitted | 2 | Fitted |
| Fid1, Fid2, Fid3 | N.A. | Fiducial Mark | Fiducial | N.A. | N.A. | Fitted | 3 | Fitted |
| L1 | 4.7µH | Inductor, 120mA, ±20%, 0.65R | 0603 | TDK Corporation | MLZ1608M4R7WT000 | Fitted | 1 | Fitted |
| L2 | 2.7nH | Inductor, 600mA, ±0.1ｎH, 120mOhm | 0201 | Murata | LQP03HQ2N7B02 | Fitted | 1 | Fitted |
| L3, L4 | 3.5nH | Inductor, 500mA, ±0.1ｎH, 170mOhm | 0201 | Murata | LQP03HQ3N5B02 | Fitted | 2 | Fitted |
| L6 | 1.0mH | Inductor, 15mA, ±10%, 24Ω | 1008 | Taiyo Yuden | LSQBA251818T102K | Not Fitted | 0 | Not Fitted |
| L7 | 2.7nH | Inductor, 800mA, ±2%, 120mΩ | 0402 | Murata | LQG15HS2N7B02D | Fitted | 1 | Fitted |
| LABEL1 | 8x8mm | PCA version label, stick-on, 8x8mm | Label | Ellco Etikett | A33511-01 | Fitted | 1 | Fitted |
| LED1 | LED RGB | RGB LED 1.60 mm x 0.80 mm SMD | LED_RGB_0603 | Kingbright Company LLC | APHF1608LSEEQBDZGKC | Fitted | 1 | Fitted |
| LED2 | LED RGB | RGB LED 1.60 mm x 0.80 mm SMD | LED_RGB_0603 | Kingbright Company LLC | APHF1608LSEEQBDZGKC | Not Fitted | 0 | Not Fitted |
| P1 | Socket 2x5, Angled | Socket 2x5, 1.27mm (50mil), Right Angle, TH | SOCKET_ANG_2x5_TH-1.27mm | WCON | 2344-205RS0CUNR3 | Fitted | 1 | Fitted |
| P2 | Pin List 1x3 | Pin List 1x3, 2.54mm (100mil) | LIST_1x3 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| Q1 | TEMT6200FX01 | Phototransistors 550nm Top View 0805 (2012 Metric) | N.A. | Vishay | TEMT6200FX01 | Not Fitted | 0 | Not Fitted |
| Q2, Q3 | RV2C010UNT2L | N-Channel MOSFET, 20V, 1A, 470mOhm, 400mW, -55°C ~ 150°C | DFN-3 | Rohm Semiconductor | RV2C010UNT2L | Not Fitted | 0 | Not Fitted |
| R1 | 1k | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Fitted | 1 | Fitted |
| R2, R3 | 10k | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Fitted | 2 | Fitted |
| R4 | 2k7 | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| R5 | 10R | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Fitted | 1 | Fitted |
| R6 | 4R7 | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Fitted | 1 | Fitted |
| R8, R9 | 1M0 | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Fitted | 2 | Fitted |
| R11 | 390R | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Fitted | 1 | Fitted |
| R12 | 2k2 | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Fitted | 1 | Fitted |
| R13 | 1k2 | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Fitted | 1 | Fitted |
| R14 | 390R | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| R15 | 2k2 | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| R16 | 1k2 | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| R17 | 100k | Resistor, ±1%, 0.05W | 0201 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| R19 | 0R | Resistor, ±5%, 0.05W | 0201 | N.A. | N.A. | Not Fitted | 0 | Not Fitted |
| SW1 | PB SW | Tactile Switch, SPNO, SMD, 260gf, 4.2x3.3x2.5mm | KLS-TS3402 | KLS Electronic | L-KLS7-TS3402-2.5-250-B-T | Fitted | 1 | Fitted |
| SW2, SW3 | PB SW | Nano-Miniature Top Actuated Tact Switch with extended life 3.0x2.6mm | SW_TACTILE_2.6x3.0MM_4-LEAD | C&K | KMT021 NGJ LHS | Not Fitted | 0 | Not Fitted |
| U1 | nRF54L15-QFAA-C00 | Multi-protocol Bluetooth Low Energy, IEEE 802.15.4 and 2.4GHz proprietary Wireless MCU | QFN-48 | Nordic Semiconductor | nRF54L15-QFAA-C00 | Fitted | 1 | Fitted |
| U3 | BME688 | Low power gas, pressure, temperature & humidity sensor | LGA-8 | Bosch | BME688 | Fitted | 1 | Fitted |
| U4 | BMI270 | 6-axis, smart, low-power, inertial measurement unit | LGA-14 | Bosch | BMI270 | Fitted | 1 | Fitted |
| U5 | ADXL367 | Micropower, 3-Axis, ±2 g/±4 g/±8 g Digital Output MEMS Accelerometer | LGA-12 | Analog Devices | ADXL367BCCZ | Fitted | 1 | Fitted |
| U6 | SKY13348-374LF | 50 MHz-6.0 GHz GaAs SPDT Switch | XFDFN-6 | Skyworks | SKY13348-374LF | Fitted | 1 | Fitted |
| U8 | MX25R6435F | Ultra low power, 64M-bit, serial multi I/O flash memory | USON-8 | Macronix | MX25R6435FZBIH3 | Not Fitted | 0 | **Fitted (project override)** |
| X1 | 32.768kHz | XTAL SMD 2012, 32.768kHz, 9pF, ±20ppm | XTAL_2012 | TXC Corporation | 9HT11-32.768KDZC-T | Fitted | 1 | Fitted |
| X2 | 32MHz | XTAL SMD 2016, 32MHz, Cl=8pF, Tot: ±40ppm | XTAL_2016 | Kyocera | CX2016DB32000D0WZRC1 | Fitted | 1 | Fitted |

### BOM metadata

- **Product Name:** nRF54L15 Tag
- **Board:** PCA20072
- **Version:** 1.0.0
- **Creation Date:** 2026-04-13
- **Stock fitted quantity total:** 64
