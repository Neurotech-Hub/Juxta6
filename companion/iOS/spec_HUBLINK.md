# Hublink BLE protocol (Juxta6)

Contract between the **Juxta6** iOS companion and **juxta6-0-prod** Tags. Service UUID: `57617368-5501-0001-8000-00805f9b34fb`. Advertising name: `JX_*` (Mobile Tag) or `JB_*` (Base Station) + last 6 hex of public address = `deviceId`.

Firmware reference: `applications/juxta6-0-prod` (current ship **6.5.0**, log schema **`jxta-nor-csv-v9`**).

## Characteristics

### 1. Node (READ) — `57617368-5505-0001-8000-00805f9b34fb`

CamelCase JSON status + settings:

```json
{
  "firmwareVersion": "6.5.0",
  "batteryLevel": 85,
  "memoryLevel": 42,
  "deviceId": "JX_XXXXXX",
  "subjectId": "001",
  "experiment": "trial-A",
  "advInterval": 2,
  "scanInterval": 30,
  "inactivityMultiplier": 1,
  "motionLogging": true,
  "isBaseStation": false
}
```

| Key | Notes |
| --- | --- |
| `firmwareVersion` | Companion accepts prefix `6.` only |
| `batteryLevel` / `memoryLevel` | 0–100 |
| `deviceId` | Current ADV name (`JX_*` or `JB_*`) |
| `advInterval` / `scanInterval` | Seconds between 1 s bursts; `0` = off; max 120 |
| `vitalsInterval` | Seconds; hard floor **60** on the Tag |
| `inactivityMultiplier` | 1–10 |
| `motionLogging` | bool |
| `isBaseStation` | `false` = Mobile Tag (`JX_`); `true` = Base Station (`JB_`) |

Unknown keys may be ignored.

### 2. Gateway (WRITE) — `57617368-5504-0001-8000-00805f9b34fb`

```json
{
  "timestamp": 1717003200,
  "sendFilenames": true,
  "latitude": 38.6270,
  "longitude": -90.1994,
  "subjectId": "001",
  "experiment": "trial-A",
  "advInterval": 2,
  "scanInterval": 30,
  "vitalsInterval": 60,
  "inactivityMultiplier": 1,
  "motionLogging": true,
  "isBaseStation": false,
  "clearMemory": true,
  "reset": true
}
```

| Key | Notes |
| --- | --- |
| `timestamp` | Unix **UTC** epoch seconds (required for logging clock) |
| `sendFilenames` | Request file listing via Filename indications |
| `latitude` / `longitude` | WGS84 degrees from the gateway phone; omit if unavailable. When both present, Tag stores last-known in NVS and writes them on the `time_set` JXS row |
| `isBaseStation` | Role / identity. Changing it **always** clears NOR CSV on the Tag (companion should also send `clearMemory: true`) |
| `clearMemory` | Erase NOR CSV regions |
| `reset` | Disconnect and enter shelf (white LED chirp every 5 s) |
| settings keys | Persisted to NVS; `experiment` may be omitted when empty; `vitalsInterval` clamped to ≥60 |

### 3. Filename (READ/WRITE/INDICATE) — `57617368-5502-0001-8000-00805f9b34fb`

- **WRITE** filename to start transfer (e.g. `JXV20260507.csv`)
- **INDICATE** listing: `name|size;name|size;EOF` (may be chunked; buffer until `EOF`)

### 4. File Transfer (READ/INDICATE) — `57617368-5503-0001-8000-00805f9b34fb`

- **INDICATE** UTF-8 body chunks; terminal `"EOF"` (or `"NFF"` if missing)

## Connection sequence

1. Connect (service UUID filter).
2. Read Node; reject if `firmwareVersion` does not start with `6.`.
3. Subscribe to Filename + File Transfer indications.
4. Gateway write: `{"timestamp": <UTC>, "sendFilenames": true}` plus optional `latitude` / `longitude` from the phone.
5. Further Gateway settings writes as needed.
6. LIST → write filename → stream File Transfer until `EOF`.

MTU: Tag negotiates large ATT MTU (companion should exchange MTU; firmware targets 247).

## Daily packages on disk (companion)

```text
Documents/
  JX_A1B2C3/          # or JB_A1B2C3 for a base
    JXS20260507.csv
    JXV20260507.csv
    JXB20260507.csv
```

## NOR CSV (schema v9)

| File | Columns |
| --- | --- |
| JXS | `unix,event,device_id,subject_id,experiment,fw_version,scan_interval_s,adv_interval_s,vitals_interval_s,ble_name,latitude,longitude` |
| JXV | `sec,motion,batt_v,temp_c,humidity` |
| JXB | `sec,peer_id,rssi` |

`sec` is day-relative UTC seconds (`unix % 86400`). Absolute time = UTC midnight of `YYYYMMDD` + `sec`. JXB `peer_id` is `X` or `B` + 6 hex (reconstruct `JX_`/`JB_` for display).
