# tag-vdd (HIL-12)

SAADC internal VDD sample for CR2032 / direct-supply tags. No FUEL divider on Tag.

## Expect

RTT lines like:

```text
idle vdd_mv=… batt_pct~… (CR2032 estimate)
adv vdd_mv=… batt_pct~… (CR2032 estimate)
```

Expect ~2.7–3.3 V on a coin cell. Overlay must use `ADC_GAIN_1_4` + `zephyr,vref-mv = <900>` (Nordic Tag); `ADC_GAIN_1` under-reads by ~4× (~900 mV).

Idle samples, then a short non-connectable adv burst for TX droop, then idle loop.

Shared helper: `lib/juxta_vdd` (`juxta_vdd_read_mv()`).

## Build / flash (user)

- Board: `nrf54l15tag/nrf54l15/cpuapp`
- Snippet: `rtt-console`
- App: `applications/tag-vdd`
