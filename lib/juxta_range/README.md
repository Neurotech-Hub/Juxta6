# Juxta ranging layer (`lib/juxta_range`)

Wraps Nordic Channel Sounding + Ranging Service (RAS). Applications must not call
`bt_le_cs_*` / RAS APIs directly — use `juxta_range_as_initiator` /
`juxta_range_as_reflector`.

Aligned with **NCS ≥ 3.3.4** `channel_sounding/ras_initiator` defaults:

- CS mode **2 + sub-mode 1** (PBR + RTT)
- Prefer **realtime ranging data** when the peer RRSP advertises it (Nordic default);
  fall back to on-demand RD + slower procedure interval
- Continuous procedures (`max_procedure_count = 0`)
- Distance-estimation sliding window of **9** samples with **median** per method
  (`ifft`, `phase_slope`, `rtt`); `distance_m` follows Nordic `best` priority
  (ifft → phase_slope → rtt)
- `bt_le_cs_remove_config()` before create and on teardown (avoids stale config
  ID / alternating HCI `0x20`)
- RAS parse path: `bt_ras_rreq_rd_subevent_data_parse` + `cs_de_calc`

## Required application Kconfig (see `applications/tag-cs/prj.conf`)

- `CONFIG_BT_CHANNEL_SOUNDING=y`
- `CONFIG_BT_RAS=y`, `CONFIG_BT_RAS_RREQ=y`, `CONFIG_BT_RAS_RRSP=y`
- `CONFIG_BT_CS_DE=y` (initiator distance estimate)
- GATT client + discovery modules used by Nordic RAS RREQ
- Recommended: `CONFIG_BT_RAS_MAX_ANTENNA_PATHS=1` for Tag HIL

## Antenna

Use the Tag board `skyworks,sky13348` path. **Do not** apply the nRF54L15 DK
`nordic,bt-cs-antenna-switch` overlay that claims `gpio1 11–14` (Tag TWI).

## NCS

Tag board + CS: **NCS v3.3.1 or newer**. If CS Kconfig symbols are missing at
build time, stop and ask — do not invent overlays from older DK samples.
