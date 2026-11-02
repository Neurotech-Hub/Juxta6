# pyJuxta: Analysis Framework and Developer Specification

**Status:** v0.1 analytical scope; `pyjuxta` 0.1.0 implements the Phase 0 loader and health views plus activity, diel and rhythms, environment, base visits, the proximity matrix, pairwise proximity, and activity synchrony (see Section 7). Input contract checked against `applications/juxta6-0-prod` firmware **6.6.0**, schema **`jxta-nor-csv-v10`** (2026-11-02).  
**Purpose:** Guide development of scientifically interpretable analyses and accompanying, beginner-friendly documentation for Juxta wearable deployments.

## 1. Goals and analytical principles

Juxta devices can be configured as **mobile** devices attached to animals (advertised as `JX_XXXXXX`) or **fixed base stations** (advertised as `JB_XXXXXX`). The analysis library, **pyJuxta**, should work with one device or a collection of devices and support three levels of analysis:

1. **Individual device:** activity, diel rhythms, environmental exposure, base-station visitation, and device health.
2. **Dyad (two animals):** BLE proximity, shared-space use, activity synchrony, and behavioral similarity.
3. **Deployment:** weighted association networks, communities, group activity, and spatiotemporal patterns.

**Scientific rule:** Keep **observations** (detections, motion counts, temperatures) separate from **interpretations** (contact, resting, social partnership, coordinated behavior). Report inferential results with measurement coverage, parameter settings, validation status, and uncertainty. A signal's correlation does *not* establish social interaction or causality [R3–R7].

## 2. Input format

Column names and time encoding below are the v10 contract in firmware. The row counts are from one external capture, mobile `JX_54F7A1` on **2026-10-31**, which is not in the repo. They describe that sample only.

| Type | Example | Columns | Rows | Meaning |
|---|---|---|---:|---|
| **JXV** | `JXV20261031.csv` | `sec,motion,batt_v,temp_c,humidity,lux` | 823 | Vitals / motion summaries |
| **JXB** | `JXB20261031.csv` | `sec,peer_id,rssi` | 5,499 | BLE peer detections and received signal strength (dBm) |
| **JXS** | `JXS20261031.csv` | `unix,event,device_id,subject_id,experiment,fw_version,scan_interval_s,adv_interval_s,vitals_interval_s,ble_name,latitude,longitude` | 18 | Device identity, acquisition settings, and events |

The row counts and the 13.8 h span below describe that one capture. They are not schema limits. That capture is not in the repo yet; do not treat 823 / 5,499 / 18 as a parser test until the files are checked in.

- In that capture, `JXV.sec` runs from 48 to 49,822 and the gap between vitals is mostly 60–61 s. `motion == 0` on about 61% of rows. That zero is a motion-summary value, not sleep or rest.
- In that capture, `JXB.sec` runs from 2,618 to 49,865, with five distinct `peer_id` values, all `B` plus six hex digits, and RSSI from −102 to −26 dBm. `B` means those peers advertised as bases. The logger also records mobiles (`X` plus six hex). This file simply did not contain any.
- `day_start.unix` of **2026-10-31 00:00:48 UTC** matches `JXV.sec == 48` because that is `unix % 86400`, not because logging started 48 s after boot. See the contract below.
- One mobile and ~13.8 h is not enough for a 24 h rhythm or for animal–animal synchrony.

### Required importer behavior (P0)

- Read one file, one device's daily files, or an entire deployment folder. Associate files with `device_id`, date, and device role using JXS and an explicit deployment manifest; never infer animal identity from filename alone.
- Convert times into a common **UTC timestamp**; preserve raw `sec`, `unix`, source filename, and any uncertainty. Treat local time and solar time as separate **derived** coordinates requiring configured timezone/location.
- Reconstruct day boundaries, assess monotonicity, and flag `boot`/`time_set` events and possible discontinuities. Treat duplicated timestamps and repeated detections intentionally.
- Apply JXS settings as time-varying metadata when appropriate; changing scan/sample intervals can bias event frequency and duration.
- Preserve unobserved periods as **missing**, not inactive, absent, or zero RSSI. Track valid device operating intervals and observed scan opportunities where known.
- Maintain an explicit mapping of `device_id -> subject_id -> role`, `peer_id -> canonical_device_id`, and base-station coordinates if supplied. Distinguish **unknown peer**, **mobile**, and **base**.
- Report data quality: recording span, completeness, sampling interval distribution, clock issues, unknown IDs, and usable overlap among devices.

### Firmware contract (`juxta6-0-prod` 6.6.0)

Checked in `juxta_log.c`, `juxta_prod.h`, `main.c`, `lib/juxta_id`, and `lib/juxta_motion`. Mobile and base run the same production loop. `isBaseStation` only changes the advertised name (`JX_` vs `JB_`).

**Time.** `JXV.sec` and `JXB.sec` are `unix % 86400` (UTC seconds after midnight). The filename date is the UTC day of that unix time. `JXS.unix` is absolute UTC seconds. Reconstruct JXV/JXB time as UTC midnight of the filename date plus `sec`. Several peers flushed from one scan share the same `sec`; that is one burst, not a duplicated clock.

**`motion`.** Count of ADXL367 samples, taken every 100 ms, where any axis moved more than 0.5 m/s² since the previous sample. The counter is cleared only when a vitals row is written (`juxta_motion_take`), not on boot or sync, so the first row after a gap contains motion accumulated since the tag last logged or booted. pyJuxta sets those rows aside by default (`exclude_after_gap`). It is not milli-g, not a vector magnitude, and not raw XYZ (XYZ is not logged). With `motionLogging` off, the firmware stores 0. That flag is not a JXS column, so a run of zeros can mean either “still” or “logging disabled.” Default vitals period is 60 s and the firmware refuses anything shorter (`JUXTA_MIN_VITALS_INTERVAL_S`).

**`batt_v`, environment.** `batt_v` is SAADC VDD in volts to two decimals (2950 mV logs as `2.95`), not a fuel-gauge state of charge. `temp_c` and `humidity` are one BME688 one-shot per vitals row, one decimal, cell left empty if the sample fails. `lux` is a whole number from the TEMT6200 with its load FET on, cell left empty if the sample fails. Empty is missing, not zero.

**`peer_id`.** Advertised `JX_ABCDEF` is stored as `XABCDEF`. `JB_ABCDEF` is stored as `BABCDEF`. The letter is the peer’s role; the six hex digits are the same as the advertised name. Reconstruct with `JX_` or `JB_` plus those six digits. Anything that is not a Juxta name is dropped and never logged.

**What one JXB row is.** Production alternates a passive scan burst and a non-connectable name-only advertise burst. They never overlap; if both are due, scan wins. Defaults are a scan every 30 s and an advertisement every 2 s (`JUXTA_DEFAULT_SCAN_INTERVAL_S`, `JUXTA_DEFAULT_ADV_INTERVAL_S`). Either interval stored as 0 turns that burst off. The companion can set either up to 120 s. Each burst is 1 s. The scan spends 500 ms on each antenna and keeps the strongest RSSI per name. At the end of the burst it writes **one row per peer**, at most **8** peers (`PEER_SLOT_COUNT`). Extra names in that burst are discarded. A missed burst is the absence of rows, not a row with a sentinel RSSI. RSSI is the controller’s dBm value stored as int8. The first log row of a UTC day creates that day’s JXS, JXV, and JXB files together, so a header-only JXB means “the day was opened,” not “a scan ran and saw nobody.”

**Mobiles hear mobiles.** Every device in production both advertises its name and scans for other `JX_` / `JB_` names. A mobile’s JXB can contain other mobiles (`X…`) and bases (`B…`). A base’s JXB can contain mobiles and other bases. Hearing each other still depends on overlap: the listener must be in its 1 s scan while the peer is in its 1 s advertise, and a per-device phase offset exists so units that booted together are not all scanning at once. `inactivityMultiplier` is stored and shown in the companion, but **6.6.0 does not apply it** to the scan cadence.

**Shelf and JXS events.** A cold boot enters the shelf (radio off, 100 ms motion poll stopped) and returns only after a magnet hold, which reboots into sync. `shelf_entry` is written only when the clock is already valid; a first boot before any sync can omit it. `shelf_exit` and `boot` are both queued at the start of that sync and written when the companion sets the clock, so both `unix` values are the phone’s timestamp, not the magnet press and not the reset instant. `time_set` is appended immediately after those deferred rows and is the only event that stores the latitude/longitude from that write. `user_connected` and `user_disconnected` are the companion link during sync. `low_battery` is the UVLO trip during vitals. `dfu_requested` is a ≥10 s magnet hold, written only if the clock is already set. `region_full_JXS`, `region_full_JXV`, and `region_full_JXB` mean that NOR region latched full. `day_start` is the first row of a new day’s JXS and copies the settings snapshot, including the last stored gateway fix when `location_valid` is set. Those coordinates are the phone’s fix, not a tag GPS track. `motion_logging` and `inactivityMultiplier` are companion settings and are **not** JXS columns. Scan, advertise, and vitals intervals on an event row are the settings at the time of that row; a mid-day change is invisible until the next event or the next `day_start`.

**Identity.** `device_id` / `ble_name` in JXS is the logger. `subject_id` is a label the companion can set; it defaults to the device name and is not, by itself, an animal id. Role is the `JX_` / `JB_` prefix. Do not infer an animal from the filename alone. The iOS export folder name is that same device id.

## 3. Individual-device analyses

| Priority | Metric | Computation / output | Scientific basis and caveat |
|---|---|---|---|
| **P0** | Device health | Samples/day, coverage, clock events, reboot counts, voltage vs. time, signal availability, configuration summary | Quality control; battery voltage is **not** a calibrated state-of-charge estimate. |
| **P0** | Activity profile | `motion` time series; histogram; hourly mean/median; fraction with `motion > 0`; configurable active threshold | Per the contract, `motion` is a count of 100 ms samples that moved more than 0.5 m/s² since the last vitals row. A threshold on that count still needs field validation [R8]. A stored 0 can also mean motion logging was turned off. |
| **P1** | Activity bouts | Contiguous active/inactive intervals, count, duration, transition rates, distributions | Use clearly named **activity-state** labels, not sleep; do not bridge large missing-data gaps [R8]. |
| **P1** | Diel activity | Clock-hour activity heatmap across dates, average 24-h activity profile, day/night differences, peak timing | An observed 24-h profile is **diel**, not proof of endogenous circadian control [R1, R2]. |
| **P1** | Rhythmic organization | Interdaily stability (IS), intradaily variability (IV), least-active 5-h window (L5), most-active 10-h window (M10), relative amplitude `RA=(M10-L5)/(M10+L5)` | Standard nonparametric rest–activity methods; use **multi-day data** and quality gates; refuse/report insufficient data rather than returning misleading scores [R1, R2]. |
| **P0** | Environmental exposure | Temperature, relative humidity, lux timeseries; extremes; summaries; light-on/light-off periods where sensor supports it | Measures **sensor microenvironment**, not core body temperature. Orientation, covering and saturation may affect light interpretation. |
| **P1** | Base-station visitation | First/last detection, per-base evidence of occupancy, visit episodes, apparent dwell, sequences of bases | Distinguish *detection* from *residence*; estimate durations only with scan cadence and gap rules exposed [R3, R4]. |
| **P2** | Environmental/activity coupling | Association of motion with light and temperature, with time-of-day controls | Exploratory; shared diel structure can create spurious correlations. |

**Rhythm implementation gate:** P1 metrics requiring repeated 24-h cycles should initially require at least **3 complete or near-complete days** (an *engineering safeguard*, not a literature-certified universal threshold), configurable in code; report number of complete days and fraction of expected observations. Do not apply standard L5/M10 estimates to a 13.8-h example.

**Suggested individual figures:** vitals dashboard; 24-h actogram (day rows, clock hour columns); activity-bout timeline; light/motion overlay; base-station RSSI/raster panel; data-quality summary.

## 4. Dyadic analyses (mobile–mobile and shared base)

The crucial distinction is between **proximity**, **simultaneous behavior**, and **similar overall behavioral profiles**. These are separate scientific constructs [R5–R7].

### 4.1 Direct BLE proximity (P1)

For mobile devices A and B, use detections of B by A and, if available, A by B. Generate:

- **Detection counts** and sampling-effort-adjusted detection rates.
- **RSSI distributions** (median, quantiles, changes over time), not uncalibrated meters.
- **Encounter episodes:** runs of repeated detections meeting a configured RSSI threshold and merge-gap rule; report observed start/end and uncertainty. Compare sensitivity across parameter settings.
- **Encounter frequency**, total observed encounter time, typical episode length, intervals between episodes, and repeated association by day.
- **Reciprocity/concordance:** how often both devices independently detect the other in the same window, accounting for scanning schedules.

**Limitations:** RSSI and detection probability depend on body orientation, habitat, antenna, and whether the 1 s advertise burst overlapped the peer’s 1 s scan. With the defaults (advertise every 2 s, scan every 30 s) most of the day is not sampled. The logged RSSI is the stronger of the two antennas in that burst, not a single packet and not a distance [R3, R4]. A missing row is a missed scan opportunity or a full 8-peer slot list, not evidence the peer was absent. Merge-gap choices can materially alter episode duration [R4]. More than eight Juxta names in one burst are silently dropped.

### 4.2 Shared-base-station co-detection (P1)

Both directions are logged, by whoever is scanning. Base S’s JXB contains a row when S heard A or B (`X…`). A’s and B’s own JXB contain a row when they heard S (`B…`). Prefer S’s log when it exists: one scanner, one clock, both animals as peers. Falling back to “both mobiles heard S” is a different measurement (each animal’s scan schedule and a shared beacon, not one receiver). Report:

- Shared-base co-detection time, repeated co-detection episodes, and station-specific overlap.
- Co-detection rate **conditional on both devices being observable**; control for each device's visitation frequency.
- Co-occurrence profiles by station and clock hour.

**Interpret as** shared detection region / shared resource use, **not direct contact**. A large base-station coverage footprint can cause co-detections between animals that are not near one another. If only mobile→base detections exist, state that the result is an **inferred shared-area indicator** rather than a direct pair-to-pair measurement.

### 4.3 Activity synchrony (P1: flagship analysis)

Align the two mobile JXV streams to a common time grid, nominally **60 s** where the data permit. Use only windows in which both animals have actual observations, with an explicit tolerance and no silent interpolation across gaps.

Calculate complementary measurements:

1. **Continuous motion synchrony:** Pearson or Spearman correlation of time-aligned `motion` (with support for transformations and a robust choice for zero-heavy series).
2. **Binary activity agreement:** percentage of aligned samples where both are active or both inactive, plus an agreement statistic adjusted for base rates (e.g., Cohen's kappa) when meaningful. Active threshold must be configurable and documented.
3. **Lagged cross-correlation:** test whether changes in one motion series systematically precede the other, within a *predefined*, biologically plausible lag window. Distinguish descriptive lag from causal leadership.
4. **Encounter-conditioned synchrony:** compare these measurements **during proximity/co-detection windows** against matched non-encounter windows with similar time-of-day and sampling coverage.
5. **Excess synchrony vs. null:** estimate the observed-minus-null statistic using time shifts or block-based resampling that retain autocorrelation and, where feasible, within-day patterns. Specify the null hypothesis and whether permutations control shared station, diel timing, and environment.

**Important:** Two diurnal animals may have high raw motion correlation simply because both move during daylight. Prefer *residualized or condition-matched synchrony* to raw correlation for any claim of a special pair relationship. Temporal association is not proof of social coordination [R5–R7].

### 4.4 Behavioral similarity (P1)

Different from synchrony: **Do A and B have alike daily patterns even when apart?**

- Compare their multi-day **24-h average activity profiles** (profile correlation or time-binned distance).
- Compare activity fraction, bout duration distributions, IS/IV/RA, and day/night allocation.
- Optionally compare temporal shift between daily profiles (phase offset), separating this from minute-by-minute synchrony.
- Produce a **behavioral similarity matrix** over mobile devices, with observation duration, data coverage, and uncertainty beside each score.

High similarity can arise from species-specific activity rhythms or shared environmental forcing; do not label it affiliation.

### 4.5 Temperature and light co-variation (P2: exploratory)

- Correlate **changes** in temperature/light, rather than raw levels alone; test against shared base-station time and time-of-day matched controls.
- A positive result may indicate similar microenvironment or synchronized exposure, **not** thermoregulation, contact, co-nesting, or social affiliation without independent evidence.
- Give temperature/light substantially less inferential weight than direct validated proximity.

**Suggested dyad figures:** two synchronized motion traces plus BLE detections; encounter raster; lag-correlation plot; scatter of encounter vs. non-encounter synchrony; comparison to permutation/null distribution.

## 5. Deployment-level analyses

| Priority | Metric | Output | Interpretation / safeguards |
|---|---|---|---|
| **P1** | Pairwise proximity matrix | A×B heatmap of normalized encounter / co-detection strength | Store observation coverage; distinguish direct pair detections from shared-base inferred associations [R3–R5]. |
| **P1** | Activity synchrony matrix | A×B heatmap of observed and *excess* synchrony | Use matched observation periods and a consistent null model across dyads [R5–R7]. |
| **P1** | Behavioral similarity matrix | A×B similarity of daily activity profiles | Present separately from encounters and social relationships. |
| **P1** | Social association network | Weighted graph: nodes are mobile animal IDs, edges represent chosen association index | Record how edge weights were derived; adjust for uneven observation effort; avoid interpreting graph edges as confirmed social bonds [R5, R6]. |
| **P2** | Network statistics | Weighted degree/strength, community assignments, edge persistence over time | Avoid excessive centrality metrics until scientifically justified; network sampling and threshold selection bias results [R5, R6]. |
| **P2** | Collective behavior | Group activity index, periods of simultaneous activity, group formation/dissolution | Adjust for number of currently observable tags and common diel drive [R6, R7]. |
| **P2** | Base-station ecology | Site visitation and transition matrices, co-occurrence by site, revisit patterns | Base stations provide **coarse spatial context**, not animal trajectories. |

**Recommended network model:** Keep **separate edge layers** for (a) mobile–mobile direct proximity, (b) shared-base co-detection, (c) excess behavioral synchrony, and (d) behavioral similarity. Do **not** introduce a single composite “social score” until weights and interpretation are validated. Network significance testing should use suitable **pre-network/permutation null models**, not merely randomizing an already-derived network [R5, R6].

## 6. Data products and public API targets

These are **suggested logical interfaces**, not a requirement to freeze naming now:

```python
import pyjuxta as jx

deployment = jx.load_deployment("data/")
deployment.summary()                # devices, roles, dates, coverage, warnings

device = deployment.device("JX_54F7A1")
device.activity()                   # activity series, states, bouts

device.environment()                # temp / humidity / lux

device.visits()                     # static-base detections / visit episodes

device.rhythms()                    # available only with sufficient multiday data

pair = deployment.pair("JX_AAAAAA", "JX_BBBBBB")
pair.proximity()
pair.synchrony()                    # aligned motion and encounter-conditioned tests
pair.behavioral_similarity()

deployment.proximity_matrix()
deployment.synchrony_matrix()
deployment.network(edge_type="proximity")
```

Each method should return **structured data** (`pandas.DataFrame` or a documented result object), separate from plotting. Each summary must include `parameters`, `quality`, `n_observations`, `coverage`, and (where appropriate) `units`, `calibration`, and `uncertainty`. Plotting should use the same result object so downloadable tables and visualizations agree.

### Documentation examples to build

1. **First 5 minutes:** import the example device; view recording span, missing data, and a JXV motion plot.
2. **Understanding a Juxta recording:** overlay JXV motion and environmental signals with JXS boot/time-set events.
3. **Where was my animal detected?** view base-station detections and RSSI, with caveats about interpreting distance.
4. **Comparing two animals:** juxtapose motion, BLE encounter intervals, raw synchrony, and encounter-conditioned synchrony.
5. **From dyads to a network:** create a weighted graph with an explicitly selected edge definition.

Every tutorial should show **actual output figures first**, then minimal executable code, then biological interpretation and limitations. Provide downloadable sample data; offer notebooks/Colab examples for users without configured Python environments.

## 7. Roadmap and acceptance criteria

### Phase 0 — Data model, QA, and individual overviews

- [x] Read/merge JXV, JXB, JXS per device and across dates, with verified time semantics (`load_deployment`: folders, zips, CSVs).
- [x] Role and identity resolution; preserve unknown peers and missing observations.
- [x] Device health, vitals overview, data availability panels; mark invalid rhythm estimates as unavailable (`availability`, `battery`, `rhythms` quality gate).
- [ ] The example deployment (`docs/utilities/pyjuxta/data/`, six tags, 30 Oct–2 Nov 2026 UTC) is now in the repo and `test_example_dataset.py` runs every analysis on it. The original single-device capture (823 JXV, 5,499 JXB, 18 JXS, five `B` peers) is still not checked in; if it is added, a regression test must preserve those counts. The contract tests cover: `sec = unix % 86400`, `X`/`B` peer ids, empty environmental cells stay missing, and peers that share a scan burst share a timestamp.

### Phase 1 — Scientifically interpretable analyses

- [x] Multi-day diel plots and rhythm metrics with quality gates (`diel`, `rhythms`). Activity bouts are not implemented.
- [x] Base-station visit analysis and pairwise encounter reconstruction with tunable RSSI/gap rules (`visits`, `proximity`, `proximity_matrix`). Shared-base co-detection (4.2) is not implemented as its own call.
- [x] Motion synchrony + lagged correlation + condition-matched/null comparisons (`synchrony`).
- [ ] Behavioral profile similarity; pairwise matrix plots.
- [ ] Weighted networks using **explicitly selected edge types** and recorded observation effort.

### Phase 2 — Validation and advanced inference

- [ ] Controlled RSSI vs. distance/orientation experiments for mobile–mobile and mobile–base configurations [R3, R4].
- [ ] Validate `motion` against behavioral video/ethogram; estimate sensitivity/specificity for activity states [R8].
- [ ] Simulated data tests: independent diurnal animals must not be falsely flagged as socially synchronized; true within-encounter coupled animals should show elevated excess synchrony.
- [ ] Environmental covariates, conditional social-network inference, and group-level analyses.

**General acceptance test:** No biological inference should be displayed without stating *what is observed*, *what is inferred*, *how complete the data are*, and *which assumptions/parameters were used*.

## 8. Peer-reviewed references

**Rhythms and motion**

- **[R1]** Van Someren, E. J. W., et al. (1999). “Bright Light Therapy: Improved Sensitivity to Its Effects on Rest–Activity Rhythms in Alzheimer Patients by Application of Nonparametric Methods.” *Chronobiology International*, 16(4), 505–518. https://doi.org/10.3109/07420529908998724 — Foundational IS/IV and nonparametric rest–activity rhythm methodology; adaptation to Juxta's motion summaries requires validation.
- **[R2]** Blume, C., Santhi, N., & Schabus, M. (2016). “'nparACT' package for R: A free software tool for the non-parametric analysis of actigraphy data.” *MethodsX*, 3, 430–435. https://doi.org/10.1016/j.mex.2016.05.006 — Implementable IS, IV, RA, L5, M10 precedents and data-length considerations.
- **[R8]** Shepard, E. L. C., et al. (2008). “Identification of animal movement patterns using tri-axial accelerometry.” *Endangered Species Research*. https://doi.org/10.3354/esr00084 — Establishes the potential and constraints of inferring animal movement/behavior from acceleration; **Juxta's current aggregate `motion` field is not tri-axial raw accelerometry**.

**Proximity and movement associations**

- **[R3]** Bettaney, E. M., James, R., St Clair, J. J. H., & Rutz, C. (2015). “Processing and visualising association data from animal-borne proximity loggers.” *Animal Biotelemetry*, 3, 27. https://doi.org/10.1186/s40317-015-0065-4 — Encounter reconstruction, temporal logging, and visualizing associations.
- **[R4]** Drewe, J. A., et al. (2012). “Performance of proximity loggers in recording intra- and inter-species interactions: a laboratory and field-based validation study.” *PLoS ONE*, 7(6), e39068. https://doi.org/10.1371/journal.pone.0039068 — Evidence for detection/encounter-duration biases and empirical need for encounter amalgamation rules.
- **[R9]** Joo, R., Etienne, M.-P., Bez, N., & Mahévas, S. (2018). “Metrics for describing dyadic movement: a review.” *Movement Ecology*, 6, 26. https://doi.org/10.1186/s40462-018-0144-2 — Conceptual distinction between proximity and movement coordination; Juxta lacks the continuous location tracks used by many approaches in this review.

**Synchrony and social networks**

- **[R5]** Farine, D. R., & Whitehead, H. (2015). “Constructing, conducting and interpreting animal social network analysis.” *Journal of Animal Ecology*, 84, 1144–1163. https://doi.org/10.1111/1365-2656.12418 — Choosing and interpreting network association metrics; sampling issues.
- **[R6]** Farine, D. R. (2017). “A guide to null models for animal social network analysis.” *Methods in Ecology and Evolution*, 8, 1309–1320. https://doi.org/10.1111/2041-210X.12772 — Pre-network null models and controlling non-social drivers of apparent associations.
- **[R7]** Hoyle, Z. E., Miller, R. A., & Rands, S. A. (2021). “Behavioural synchrony between fallow deer *Dama dama* is related to spatial proximity.” *BMC Ecology and Evolution*, 21, 79. https://doi.org/10.1186/s12862-021-01814-9 — Empirical relationship between pair proximity and activity synchrony; explicit warning about environmental/common-cause explanations.

**Highly relevant recent application**

- **[R10]** Beauvieux, A., et al. (2026). “Networking the underground: tracking social associations in burrowing mammals using proximity-light-sensor biologgers.” *Animal Behaviour*, 241, article 123730. https://doi.org/10.1016/j.anbehav.2026.123730 — 2026 study of biologging-based associations in Columbian ground squirrels; potentially useful precedent for Juxta's combination of proximity and environmental/light sensing.

---

**Scientific scope note:** The cited studies support measurement strategies and validation practices, not a claim that specific Juxta outputs are already validated biomarkers of behavior. Thresholds, data-coverage gates, null-model design, and interpretation must be documented as design decisions and revised after field validation.
