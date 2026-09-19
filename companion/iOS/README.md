# Juxta6 (iOS companion)

iPhone companion for **Juxta6-0** Tags (`juxta6-0-prod`, firmware **6.1.0**, schema **`jxta-nor-csv-v6`**). Connect over Bluetooth Low Energy (Hublink), sync session settings, transfer daily CSV packages, and inspect plots offline.

Open **`Juxta6.xcodeproj`**, scheme **Juxta6**, on a physical iPhone (BLE is not available in the Simulator).

Bundle ID: `edu.wustl.neurotechhub.Juxta6`.

Developed by the [Neurotech Hub](https://neurotechhub.wustl.edu) at Washington University in St. Louis.

## Requirements

- iOS 17.0+
- Xcode 15.0+
- Physical Bluetooth-enabled iPhone
- Tag running Juxta6 firmware **6.x** (current: `6.1.0`)

## Features

- **BLE scan & connect** — Hublink service UUID; peripherals advertise as `JX_*`
- **Firmware gate** — Node `firmwareVersion` must start with `6.`; otherwise disconnect
- **Device settings** — subject, experiment, adv/scan interval (0 = off, else 1–120 s), inactivity multiplier (1–10), motion logging
- **Daily packages** — transfer `JXV` / `JXS` / `JXB` + `YYYYMMDD` into `Documents/<device_id>/`
- **Packages / plots / terminal / info** tabs

## Usage

1. Device tab → **Scan** → **Connect**.
2. App reads Node JSON, then writes Gateway `{"timestamp": <UTC epoch>, "sendFilenames": true}`.
3. Select a day package → **Transfer Selected**.
4. Browse offline under **Packages**; plot vitals / BLE activity; **Shelf Mode** / **Clear Memory** as needed.

## CSV formats (schema v6)

Filenames use the UTC calendar day (`JX{S|V|B}YYYYMMDD.csv`). Row times in JXV/JXB are **day-relative seconds** (`sec` = unix % 86400 UTC). Reconstruct absolute UTC as midnight of the filename date + `sec`.

### Vitals — `JXV<YYYYMMDD>.csv`

```csv
sec,motion,batt_v,temp_c
7200,3,2.95,27
```

### Settings — `JXS<YYYYMMDD>.csv`

Absolute `unix` event rows (including `day_start`). Preview / View All only.

### BLE Activity — `JXB<YYYYMMDD>.csv`

```csv
sec,peer_id,rssi
7205,AABBCC,-56
```

`peer_id` is stored without the constant `JX_` prefix. Observer column is omitted (device id is in the filename / package folder).

## BLE protocol

See [`spec_HUBLINK.md`](spec_HUBLINK.md). UUIDs match `juxta6-0-prod`.

### Node (READ) — keys used

```json
{
  "firmwareVersion": "6.1.0",
  "batteryLevel": 85,
  "memoryLevel": 42,
  "deviceId": "JX_XXXXXX",
  "subjectId": "001",
  "experiment": "trial-A",
  "advInterval": 5,
  "scanInterval": 30,
  "inactivityMultiplier": 1,
  "motionLogging": true
}
```

### Gateway (WRITE)

UTC `timestamp`, `sendFilenames`, settings fields, `clearMemory`, `reset` (shelf). Intervals clamped to 0 or 1–120.

## Development

- Primary UI + BLE: [`Juxta6/ContentView.swift`](Juxta6/ContentView.swift)
- Firmware contract: [`applications/juxta6-0-prod`](../../applications/juxta6-0-prod) in this monorepo
- Agents: do not `west`-build this tree; open in Xcode only

## Firmware update (DFU)

Juxta6 does not flash firmware yet. Use magnet DFU cue + Nordic **nRF Connect** when images are available.

## License

Proprietary — see the repository [LICENSE](../../LICENSE). Reuse requires explicit written permission.
