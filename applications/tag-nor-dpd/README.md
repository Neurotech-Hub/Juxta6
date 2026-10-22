# tag-nor-dpd

Isolated HIL test for MX25L3233F deep power-down (DPD) through Zephyr runtime PM,
exactly as `juxta6-0-prod` 6.5.2 used it (`has-dpd`, `zephyr,pm-device-runtime-auto`,
`CONFIG_PM_DEVICE_RUNTIME=y`, driver default `CONFIG_SPI_NOR_ACTIVE_DWELL_MS=10`,
`zephyr,deferred-init`, 500 ms settle before `device_init`).

Question answered: can DPD make reads of **erased** flash return non-`0xFF` bytes
(or corrupt valid data / appended rows)? In prod, that would make the boot-time
`find_eof_or_erased` scan overshoot the real end of a file, like the `0x20200`
length seen in the bad JXV.

## Safety

Only the last 64 KB (`0x3F0000`-`0x3FFFFF`) is erased/written: that is prod's
checkpoint ring, so a prod tag loses only checkpoints. JXS/JXV/JXB regions are
never written. Everything below `0x3F0000` is read once (timing check).

## Build (user runs in nRF Connect)

- Board: `nrf54l15tag/nrf54l15/cpuapp`, snippet `rtt-console`
- Runtime PM **on** (matches 6.5.2): default build
- Control, runtime PM **off**: add `-DEXTRA_CONF_FILE=rpm_off.conf`

## What it does

1. **Fresh boot**: erase scratch, write ~29.7 KB of CSV-like rows (no `0xFF`/`0x00`).
2. **Stress (a)**: 2000 back-to-back scans (256 B reads until first `0xFF`, same as prod, plus
   byte-compare and a 4 KB erased-tail check).
3. **Stress (b)**: 500 scans separated by 11-20 ms (chip is in DPD, every scan resumes it).
4. **Stress (c)**: 500 windows of 16 reads with 5-15 ms between reads (races the 10 ms dwell
   `put_async` against the next `get`).
5. **Write path**: append 200 rows with 0-20 ms random gaps; each row is read back with the
   following 16 bytes required to be erased.
6. **Full read timing**: 4 KB reads of `0x000000`-`0x3EFFFF`, reported as
   `read_3p94MB_ms` (validates the ~4-5 s estimate for the 6.5.3 clearMemory scan).
7. **Boot loop**: 200 cold reboots. Each boot: 500 ms settle, deferred `device_init`, immediate
   scan. Odd boots reboot right after the last read (chip awake at reset); even boots wait
   100 ms first (chip in DPD at reset).

Total runtime is on the order of 10-20 minutes.

## RTT output

Any failure logs `[DPD] MISMATCH ...` with offset, expected/got byte, chunk head hexdump,
and `pm_state`. The run ends with this line, repeated every 10 s:

```
[DPD] RESULT PASS|FAIL rpm=on|off boots=N scans=N scan_fail=N write_fail=N read_fail=N read_3p94MB_ms=N
```

Pass criterion: `PASS` with `rpm=on` **and** with the `rpm_off.conf` control. If only
`rpm=on` fails, DPD is the cause; keep it disabled in prod. If both pass, DPD is ruled out
and prod can re-enable it (see the note in the prod overlay/prj.conf).

The test restarts from a fresh erase if RAM state is lost (power cycle).
