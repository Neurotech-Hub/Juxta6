# tag-bme688 (HIL-13)

One-shot BME688 ambient temperature + relative humidity on nRF54L15 Tag
(`bme688@76` on shared TWI with ADXL367). BMI270 / ADXL left unused.

## Expect

RTT:

```text
[PROBE] bme688 ok
[SAMPLE] temp_c=… humidity_pct=…
[SUSPEND] bme688 ok
```

Sane room values are roughly `temp_c` ~15–35 and `humidity_pct` ~20–80 (lab-dependent; not a calibration gate). Then idle for PPK2 if desired.

## Build / flash (user)

- Board: `nrf54l15tag/nrf54l15/cpuapp`
- Snippet: `rtt-console`
- App: `applications/tag-bme688`
