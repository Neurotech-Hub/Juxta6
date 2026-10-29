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

## Build / flash (user)

- Board: `nrf54l15tag/nrf54l15/cpuapp`
- Snippet: `rtt-console`
- App: `applications/tag-light`
