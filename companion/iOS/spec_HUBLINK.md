# Hublink BLE protocol (Juxta6)

Contract between the **Juxta6** iOS companion and **juxta6-0-prod** Tags. Service UUID: `57617368-5501-0001-8000-00805f9b34fb`. Advertising name: `JX_*` (last 6 hex of public address) = `deviceId`.

Firmware reference: `applications/juxta6-0-prod` (current ship **6.1.0**, log schema **`jxta-nor-csv-v6`**).

## Characteristics

### 1. Node (READ) — `57617368-5505-0001-8000-00805f9b34fb`

CamelCase JSON status + settings:

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

| Key | Notes |
| --- | --- |
| `firmwareVersion` | Companion accepts prefix `6.` only |
| `batteryLevel` / `memoryLevel` | 0–100 |
| `advInterval` / `scanInterval` | Seconds between 1 s bursts; `0` = off; max 120 |
| `inactivityMultiplier` | 1–10 |
| `motionLogging` | bool |

Unknown keys may be ignored.

### 2. Gateway (WRITE) — `57617368-5504-0001-8000-00805f9b34fb`

```json
{
  "timestamp": 1717003200,
  "sendFilenames": true,
  "subjectId": "001",
  "experiment": "trial-A",
  "advInterval": 5,
  "scanInterval": 30,
  "inactivityMultiplier": 1,
  "motionLogging": true,
  "clearMemory": true,
  "reset": true
}
```

| Key | Notes |
| --- | --- |
| `timestamp` | Unix **UTC** epoch seconds (required for logging clock) |
| `sendFilenames` | Request file listing via Filename indications |
| `clearMemory` | Erase NOR CSV regions |
| `reset` | Disconnect and enter shelf (System OFF) |
| settings keys | Persisted to NVS; `experiment` may be omitted when empty |

### 3. Filename (READ/WRITE/INDICATE) — `57617368-5502-0001-8000-00805f9b34fb`

- **WRITE** filename to start transfer (e.g. `JXV20260507.csv`)
- **INDICATE** listing: `name|size;name|size;EOF` (may be chunked; buffer until `EOF`)

### 4. File Transfer (READ/INDICATE) — `57617368-5503-0001-8000-00805f9b34fb`

- **INDICATE** UTF-8 body chunks; terminal `"EOF"` (or `"NFF"` if missing)

## Connection sequence

1. Connect (service UUID filter).
2. Read Node; reject if `firmwareVersion` does not start with `6.`.
3. Subscribe to Filename + File Transfer indications.
4. Gateway write: `{"timestamp": <UTC>, "sendFilenames": true}`.
5. Further Gateway settings writes as needed.
6. LIST → write filename → stream File Transfer until `EOF`.

MTU: Tag negotiates large ATT MTU (companion should exchange MTU; firmware targets 247).

## Daily packages on disk (companion)

```text
Documents/<device_id>/
  JXV<YYYYMMDD>.csv
  JXS<YYYYMMDD>.csv
  JXB<YYYYMMDD>.csv
```

### Schema v6 columns

| File | Header |
| --- | --- |
| JXV | `sec,motion,batt_v,temp_c` |
| JXB | `sec,peer_id,rssi` |
| JXS | absolute `unix` event CSV (includes `day_start`) |

`sec` is day-relative UTC seconds (`unix % 86400`). Absolute time = UTC midnight of `YYYYMMDD` + `sec`. JXB `peer_id` has no `JX_` prefix.
