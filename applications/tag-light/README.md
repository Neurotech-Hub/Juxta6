# tag-light (HIL-15)

TEMT6200FX01 phototransistor with a FET-gated 2.7 kΩ load.

| Net | Pin | Role |
| --- | --- | --- |
| `LIGHT_SENSE` | `P1.14` (SAADC AIN7) | Phototransistor emitter. Collector is VDD. |
| `LHT_SNS_CTRL` | `P2.06` | RV2C010UNT2L gate, active high. Source is GND. Drain is the other end of the 2.7 kΩ resistor from `LIGHT_SENSE`. |

## Expect

Each pass prints the unloaded sample, then the loaded sample:

```text
ctrl=0 light_mv=…
ctrl=1 light_mv=…
```

`ctrl=1` connects the 2.7 kΩ load to GND, so that reading should move when the phototransistor is covered and uncovered. `ctrl=0` opens the FET. The pair repeats about once a second, and the load is left off between passes.

Overlay uses `ADC_GAIN_1_4` and a 0.9 V reference (full scale 3.6 V) because the emitter can rise toward VDD.

## Calibration

`juxta6-0-prod` turns the `ctrl=1` millivolt reading into lux. The points below are the raw record of that fit.

**Method.** Daylight-spectrum light panel, intensity varied, with a calibrated UPRtek MG350S as the reference meter. `light_mv` is this app's `ctrl=1` reading, which is the TEMT6200 emitter with the 2.7 kΩ load connected to ground.

| mV | lux |
| --- | --- |
| 10 | 84.07 |
| 72 | 348.2 |
| 128 | 555.5 |
| 228 | 922 |
| 500 | 1981 |
| 1306 | 5819 |
| 2268 | 11871 |

**Fit.** Linear through the origin: `lux = 4.99376 * mV`, R² = 0.9938. Production clamps millivolts at 0 before multiplying, so the result is never negative, and stores it as a whole number. The factor is `JUXTA_LIGHT_LUX_PER_MV` in `applications/juxta6-0-prod/src/juxta_light.h`.

**Range.** The points cover 10 to 2268 mV. A loaded reading above 2268 mV is an extrapolation.

## Build / flash (user)

- Board: `nrf54l15tag/nrf54l15/cpuapp`
- Snippet: `rtt-console`
- App: `applications/tag-light`
