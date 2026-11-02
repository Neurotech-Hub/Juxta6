# Example deployment

A home deployment of six Juxta6-0 tags, firmware **6.6.0**, schema **`jxta-nor-csv-v10`**. Each tag has one folder of daily `JXS`, `JXV`, and `JXB` files for **2026-10-30 through 2026-11-02** (UTC dates in the filenames). Settings on every tag are the firmware defaults: scan every 30 s, advertise every 2 s, vitals every 60 s. `subject_id` was left as the device id, so the names below are deployment labels, not a column in the CSV.

Two tags moved with a person and a dog. Four stayed in the house.

| Device id | Folder | Where it was |
| --- | --- | --- |
| `JX_54F7A1` | `JX_54F7A1 2/` | Human, in a pocket (mobile) |
| `JX_5C0848` | `JX_5C0848/` | Dog, on a Labrador’s leash (mobile) |
| `JB_83FA0F` | `JB_83FA0F/` | Garage |
| `JB_6A9D3A` | `JB_6A9D3A/` | Bedroom |
| `JB_24563F` | `JB_24563F/` | Work desk |
| `JB_0A3660` | `JB_0A3660/` | Kitchen |

The Human tag was reset several times and powered off for data uploads and maintenance. That shows up as repeated `shelf_entry` / `shelf_exit` / `time_set` cycles, mostly on 31 Oct and 1 Nov, and as fewer vitals rows than the other five tags over the same four days. The house tags, and the dog’s tag, were also shelved near the end of 1 Nov and synced again on 2 Nov. The bedroom tag has one extra sync on 31 Oct.

Latitude and longitude on some `JXS` rows are the phone’s fix at sync. They are still the values the tags logged. They are not a track of the person or the dog.

## Files

```
<data>/<DEVICE_ID>/JX{S,V,B}YYYYMMDD.csv
```

The Human folder is named `JX_54F7A1 2` because that is how it was copied in. Inside the files, `device_id` is `JX_54F7A1`.

| Prefix | Contents | Columns |
| --- | --- | --- |
| `JXV` | Vitals | `sec,motion,batt_v,temp_c,humidity,lux` |
| `JXB` | Peers heard in a scan burst | `sec,peer_id,rssi` |
| `JXS` | Identity, settings, and events | `unix` plus the settings columns, `latitude`, `longitude` |

`sec` is UTC seconds after midnight. `peer_id` is `X` or `B` plus six hex digits (`X54F7A1` is the Human tag, `X5C0848` is the dog). The dog tag was configured as a base during the deployment. In this copy its folder, its JXS `device_id` / `subject_id` / `ble_name`, and the `B5C0848` peer ids in the other tags’ JXB files are rewritten to the mobile identity. The logging rules are the firmware contract in `agents/pyJuxta_Analysis_Specification.md`.
