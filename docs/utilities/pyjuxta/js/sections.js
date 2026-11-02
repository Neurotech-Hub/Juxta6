/* The walkthrough, one entry per question.
 *
 * Each section lists its settings (controls), and for every figure the exact Python that runs.
 * Readouts are written from what pyjuxta returns (Result.summary / tables); no number on this
 * page is typed in by hand. `S` is the loaded deployment: { ids, mobiles, bases, name(id), ref(id) }. */

const U = (() => {
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
  const has = (v) => v !== null && v !== undefined && !Number.isNaN(v);
  const n = (v, d = 1) => (has(v) ? Number(v).toFixed(d) : "n/a");
  const pct = (v, d = 0) => (has(v) ? (v * 100).toFixed(d) + "%" : "n/a");
  const clock = (h) => {
    if (!has(h)) return "n/a";
    let hh = Math.floor(h), mm = Math.round((h - hh) * 60);
    if (mm === 60) { hh += 1; mm = 0; }
    return String(hh % 24).padStart(2, "0") + ":" + String(mm).padStart(2, "0");
  };
  const mins = (m) => {
    if (!has(m)) return "n/a";
    if (m < 90) return n(m, m < 10 ? 1 : 0) + " min";
    return n(m / 60, 1) + " h";
  };
  const pl = (k, word, many) => `${k} ${k === 1 ? word : many || word + "s"}`;
  const str = (s) => JSON.stringify(s);
  const list = (a) => "[" + a.map(str).join(", ") + "]";
  return { esc, has, n, pct, clock, mins, pl, str, list };
})();

// Missing stretches inside a recording that pyJuxta may paint over (minutes). Infinity is any length.
const INPAINT_OPTS = [[0, "don't fill"], [30, "30 min"], [60, "1 h"], [120, "2 h"], [180, "3 h"], [360, "6 h"], [Infinity, "any length"]];
const INPAINT_HELP = "A pause inside the recording (a phone download, a reboot) up to this long is bridged by a straight line. The start and end are never extended, and filled stretches are marked.";
const pyInpaint = (v) => (v === Infinity ? 'float("inf")' : String(v));
const BIN_OPTS = (vals) => vals.map((v) => [v, v < 3600 ? `${v / 60} min` : `${v / 3600} h`]);
const DIRECTIONS = [
  ["either", "either tag heard the other"],
  ["mobile_hears_base", "the mobile heard the base"],
  ["base_hears_mobile", "the base heard the mobile"],
];
const DIRECTIONS_PAIR = [
  ["either", "either tag heard the other"],
  ["a_hears_b", "first hears second"],
  ["b_hears_a", "second hears first"],
];

const SECTIONS = [
  /* 1 -------------------------------------------------------------------- */
  {
    id: "recorded",
    title: "What did each tag record?",
    question:
      "Before asking anything about behavior, find out when each tag was actually logging. A gap in the data is missing time, not quiet time, and every later step has to respect that.",
    tryThis: "Hover a bar to see when a session started and ended. The spaces between bars are missing time, not quiet time. Diamonds are phone syncs.",
    controls: [],
    outputs: [
      {
        key: "availability",
        code: () => `res = dep.availability()`,
        read(r, p, S) {
          const s = r.summary;
          return `<p><b>${s.devices}</b> tags (${s.mobiles} mobile, ${s.bases} base) logged over <b>${U.n(s.span_h, 1)} h</b> in ${U.pl(s.sessions, "session")}, with ${U.pl(s.phone_syncs, "phone sync")}. The least covered was <b>${U.esc(S.name(s.least_covered))}</b>, recording ${U.pct(s.least_coverage)} of its own span.</p>`;
        },
        tables: [
          {
            key: "by_device",
            cols: [["label", "Device"], ["role", "Role"], ["recorded_h", "Recorded (h)", 1], ["coverage", "Coverage", "pct"], ["sessions", "Sessions"], ["syncs", "Syncs"], ["scan_interval_s", "Scan every (s)"]],
          },
        ],
      },
      {
        key: "battery",
        heading: "Is any tag close to dying?",
        code: () => `res = dep.battery()`,
        read(r, p, S) {
          const s = r.summary;
          if (!U.has(s.lowest_v)) return "";
          const close = s.headroom_to_cutoff_v < 0.15 ? " That is close to the cutoff." : "";
          return `<p>The lowest reading was <b>${U.n(s.lowest_v, 2)} V</b> on <b>${U.esc(S.name(s.lowest_device))}</b>, ${U.n(s.headroom_to_cutoff_v, 2)} V above the firmware cutoff.${close}</p>`;
        },
      },
    ],
  },

  /* 2 -------------------------------------------------------------------- */
  {
    id: "activity",
    title: "When was each mobile moving?",
    question:
      "The accelerometer counts how many 100 ms samples in each minute showed movement. Binned on a fixed clock, that shows when each tag was active, and the grey bands show where it was not recording at all.",
    tryThis: "Raise the threshold to count only vigorous minutes. Adjust the smoothing to see the broad shape of the day instead of single minutes.",
    needs: (S) => (S.ids.length ? null : "No devices were loaded."),
    controls: [
      { id: "devices", type: "devices", label: "Devices", pool: "all", def: (S) => (S.mobiles.length ? S.mobiles : S.ids) },
      { id: "bin_s", type: "select", label: "Bin width", options: BIN_OPTS([60, 300, 600, 900, 1800, 3600]), value: 900, help: "Readings inside a bin are combined. Bins with too little coverage are left empty." },
      { id: "active_threshold", type: "range", label: "A minute counts as active at motion ≥", min: 1, max: 300, step: 1, value: 1, fmt: (v) => `${v}`, help: "motion is a count of 100 ms samples in a minute (0 to 600). 1 means any movement at all." },
      { id: "smooth_bins", type: "range", label: "Smoothing", min: 1, max: 12, step: 1, value: 6, fmt: (v) => (v === 1 ? "none" : `${v} bins`), help: "A centered moving average. It never fills a gap." },
      { id: "signal", type: "select", adv: true, label: "Show", options: [["active_frac", "share of minutes active"], ["motion_mean", "mean motion count"]], value: "active_frac", help: "" },
      { id: "inpaint", type: "select", adv: true, label: "Fill gaps up to", options: INPAINT_OPTS, value: 0, help: INPAINT_HELP },
    ],
    outputs: [
      {
        key: "activity",
        code: (p, S) =>
          `res = dep.activity(${U.list(p.devices.map(S.ref))}, bin_s=${p.bin_s}, active_threshold=${p.active_threshold}, smooth_bins=${p.smooth_bins}, signal=${U.str(p.signal)}, inpaint=${pyInpaint(p.inpaint)})`,
        read(r, p, S) {
          const rows = r.rec.by_device || [];
          const lines = rows.map(
            (d) =>
              `<li><b>${U.esc(d.label)}</b> registered movement in ${U.pct(d.active_share)} of its recorded minutes${U.has(d.motion_median_when_active) ? `; when it did, the median count was ${U.n(d.motion_median_when_active, 0)}` : ""}.</li>`
          );
          return `<ul>${lines.join("")}</ul>`;
        },
        tables: [
          {
            key: "by_device",
            cols: [["label", "Device"], ["observed_h", "Observed (h)", 1], ["active_share", "Active minutes", "pct"], ["zero_share", "Zero-motion minutes", "pct"], ["motion_mean", "Mean motion", 1], ["rows_excluded_after_gap", "Rows set aside after a gap"]],
          },
        ],
      },
    ],
  },

  /* 3 -------------------------------------------------------------------- */
  {
    id: "day",
    title: "What does a typical day look like?",
    question:
      "Stack the days on top of each other and average them by time of day. Then ask a stricter question: is there a stable daily rhythm, or just a few days that happened to look alike? pyJuxta only reports rhythm numbers when the data can support them.",
    tryThis:
      "Switch gap filling off and watch a day with a pause stop counting as complete. Then open the rhythm settings and raise the number of required days to the library's 3, or change the coverage a day needs. See what the library says when the evidence is thin, and what changes when it is not.",
    needs: (S) => (S.ids.length ? null : "No devices were loaded."),
    controls: [
      { id: "device", type: "device", label: "Device", pool: "all", def: (S) => S.mobiles[0] || S.ids[0] },
      { id: "bin_s", type: "select", label: "Time-of-day bin", options: BIN_OPTS([900, 1800, 3600]), value: 1800 },
      { id: "signal", type: "select", label: "Show", options: [["active_frac", "share of minutes active"], ["motion_mean", "mean motion count"]], value: "active_frac" },
      { id: "active_threshold", type: "range", label: "A minute counts as active at motion ≥", min: 1, max: 300, step: 1, value: 1, fmt: (v) => `${v}` },
      { id: "inpaint", type: "select", label: "Fill gaps up to", options: INPAINT_OPTS, value: 180, help: INPAINT_HELP },
      { id: "min_days", type: "range", adv: true, label: "Rhythm: complete days required", min: 1, max: 7, step: 1, value: 1, fmt: (v) => `${v}`, help: "The library's own default is 3. The example only holds one day that can be complete, so this starts at 1; raise it to see the library decline. Interdaily stability needs at least two complete days whatever this is set to." },
      { id: "min_day_coverage", type: "range", adv: true, label: "Rhythm: share of a day that must be recorded", min: 0.3, max: 1, step: 0.05, value: 0.9, fmt: (v) => `${Math.round(v * 100)}%`, help: "A day below this does not count as complete." },
    ],
    outputs: [
      {
        key: "diel",
        code: (p, S) =>
          `res = dep.diel(${U.str(S.ref(p.device))}, bin_s=${p.bin_s}, active_threshold=${p.active_threshold}, signal=${U.str(p.signal)}, inpaint=${pyInpaint(p.inpaint)})`,
        read(r, p, S) {
          const s = r.summary;
          const f = (v) => (p.signal === "active_frac" ? U.pct(v) : U.n(v, 1));
          return `<p>Across <b>${U.pl(s.local_days_with_data, "local day")}</b> with data (${s.complete_days} complete), the most active time of day was around <b>${U.clock(s.peak_clock_h)}</b> (${f(s.peak_value)}) and the quietest around <b>${U.clock(s.quietest_clock_h)}</b> (${f(s.quietest_value)}).</p>`;
        },
      },
      {
        key: "rhythms",
        heading: "Can we call it a rhythm?",
        code: (p, S) =>
          `res = dep.rhythms(${U.str(S.ref(p.device))}, min_days=${p.min_days}, min_day_coverage=${p.min_day_coverage}, inpaint=${pyInpaint(p.inpaint)})`,
        read(r, p, S) {
          const s = r.summary;
          const filled = s.filled_hours ? ` (${U.pl(s.filled_hours, "hour")} filled in across the recording)` : "";
          if (!s.available) {
            return `<p><b>Not reported.</b> ${U.pl(s.complete_days, "complete day")} found${filled}; ${s.min_days} required. A rest-activity rhythm needs repeated whole days, so the library declines to compute numbers it could not stand behind.</p>`;
          }
          return `<p>From <b>${U.pl(s.complete_days, "complete day")}</b>${filled}: stability (IS) <b>${U.n(s.IS, 2)}</b>, fragmentation (IV) <b>${U.n(s.IV, 2)}</b>, least-active five hours start near <b>${U.clock(s.L5_onset_h)}</b>, most-active ten hours near <b>${U.clock(s.M10_onset_h)}</b>, relative amplitude <b>${U.n(s.RA, 2)}</b>.</p>`;
        },
      },
    ],
  },

  /* 4 -------------------------------------------------------------------- */
  {
    id: "sensors",
    title: "What did each tag sense?",
    question:
      "Each tag also logs temperature, humidity, and light. Comparing them tells you which tags lived in rooms, which were carried, and whether a reading belongs to a place or to a body.",
    tryThis: "Switch to light and see which tags sat in a dark pocket and which sat in a lit room. Narrow the devices to compare two places directly.",
    needs: (S) => (S.ids.length ? null : "No devices were loaded."),
    controls: [
      { id: "variable", type: "select", label: "Measurement", options: [["temp_c", "Temperature"], ["humidity", "Humidity"], ["lux", "Light"]], value: "temp_c" },
      { id: "bin_s", type: "select", label: "Bin width", options: BIN_OPTS([300, 900, 1800, 3600]), value: 900 },
      { id: "devices", type: "devices", label: "Devices", pool: "all", def: (S) => S.ids },
    ],
    outputs: [
      {
        key: "environment",
        code: (p, S) => `res = dep.environment(${U.str(p.variable)}, ${U.list(p.devices.map(S.ref))}, bin_s=${p.bin_s})`,
        read(r, p, S) {
          const s = r.summary;
          if (!U.has(s.highest)) return "";
          return `<p>Highest: <b>${U.esc(S.name(s.highest_device))}</b> at ${U.n(s.highest, 1)} ${U.esc(s.unit)}. Lowest: <b>${U.esc(S.name(s.lowest_device))}</b> at ${U.n(s.lowest, 1)} ${U.esc(s.unit)}. Widest swing: <b>${U.esc(S.name(s.widest_range_device))}</b> (${U.n(s.widest_range, 1)} ${U.esc(s.unit)}).</p>`;
        },
        tables: [{ key: "by_device", cols: [["label", "Device"], ["role", "Role"], ["samples", "Samples"], ["missing", "Missing"], ["min", "Min", 1], ["median", "Median", 1], ["max", "Max", 1]] }],
      },
    ],
  },

  /* 5 -------------------------------------------------------------------- */
  {
    id: "visits",
    title: "Where was each mobile?",
    question:
      "Base stations stay put, so each time a mobile hears one it gives a clue about where the mobile was. pyJuxta groups sightings into visits, and also marks the strongest base in each window. Signal strength depends on walls and orientation, so this is a hint about place, not a position.",
    tryThis:
      "Make the RSSI threshold stricter to keep only close encounters, then widen the merge gap to turn a leaky doorway into one long visit. Watch how the share of time located changes.",
    needs: (S) => (S.mobiles.length && S.bases.length ? null : "This step needs at least one mobile tag and one base station. Set roles in the table above if a tag was configured the other way."),
    controls: [
      { id: "mobile", type: "device", label: "Mobile", pool: "mobiles", def: (S) => S.mobiles[S.mobiles.length - 1] },
      { id: "min_rssi", type: "range", label: "Weakest signal that counts", min: -100, max: -50, step: 1, value: -85, fmt: (v) => `${v} dBm`, help: "A sighting weaker than this is ignored. Higher (closer to 0) means closer." },
      { id: "merge_gap_s", type: "range", label: "Join sightings closer than", min: 30, max: 900, step: 30, value: 180, fmt: (v) => U.mins(v / 60), help: "Sightings separated by less than this are one visit." },
      { id: "min_detections", type: "range", label: "Sightings needed for a visit", min: 1, max: 10, step: 1, value: 2, fmt: (v) => `${v}`, help: "A single stray sighting is not a visit." },
      { id: "nearest_bin_s", type: "select", label: "Strongest-base window", options: BIN_OPTS([60, 300, 600, 900]), value: 300 },
      { id: "direction", type: "select", adv: true, label: "Count a sighting when", options: DIRECTIONS, value: "either", help: "Both tags log what they hear, so you can ask either direction separately." },
      { id: "min_margin_db", type: "range", adv: true, label: "Strongest must lead by", min: 0, max: 10, step: 0.5, value: 3, fmt: (v) => `${v} dB`, help: "A window where the top two bases are closer than this is marked ambiguous." },
      { id: "agg", type: "select", adv: true, label: "Signal per window", options: [["max", "strongest"], ["median", "median"]], value: "max" },
    ],
    outputs: [
      {
        key: "visits",
        code: (p, S) =>
          `res = dep.visits(${U.str(S.ref(p.mobile))}, min_rssi=${p.min_rssi}, merge_gap_s=${p.merge_gap_s}, min_detections=${p.min_detections}, direction=${U.str(p.direction)}, nearest_bin_s=${p.nearest_bin_s}, min_margin_db=${p.min_margin_db}, agg=${U.str(p.agg)})`,
        read(r, p, S) {
          const s = r.summary;
          const top = s.top_base ? `<b>${U.esc(S.name(s.top_base))}</b> was strongest most often (${U.pct(s.top_base_share_of_located)} of located windows)` : "No base was strongest in any window";
          return `<p>Over <b>${U.n(s.recorded_h, 1)} h</b> recorded, <b>${U.pl(s.episodes, "visit")}</b> were found, and a strongest base could be named for <b>${U.pct(s.located_share)}</b> of the time. ${top}; ${U.pct(s.ambiguous_share)} of windows were ambiguous.</p>`;
        },
        tables: [
          {
            key: "by_base",
            cols: [["label", "Base"], ["episodes", "Visits"], ["episode_hours", "Visit time (h)", 1], ["rssi_median", "Median RSSI (dBm)", 0], ["share_of_located", "Share of located time", "pct"]],
          },
        ],
      },
    ],
  },

  /* 6 -------------------------------------------------------------------- */
  {
    id: "matrix",
    title: "Who heard whom?",
    question:
      "Every tag both listens and advertises, so you can lay the whole deployment out as a grid: who heard whom, and how often. Rows listen, columns are heard. It is a quick check that the house hangs together before you trust anything pairwise. The network view takes one row of that grid and draws it with a single tag at the centre.",
    tryThis: "In the grid, switch to median signal to see strength instead of how often; a cell that is bright one way and dark the other is worth a look at scan and advertise settings. Then try the network and pick different centre tags: nearer the centre means a stronger signal.",
    needs: (S) => (S.ids.length >= 2 ? null : "This step needs at least two tags."),
    controls: [
      { id: "view", type: "select", label: "View", options: [["grid", "grid: every pair at once"], ["network", "network: one tag at the centre"]], value: "grid", help: "Both are built from the same sightings, so the numbers agree." },
      { id: "center", type: "device", label: "Centre tag", pool: "all", def: (S) => S.mobiles[0] || S.ids[0], show: (p) => p.view === "network", help: "Shows what this tag heard. Pick another to see what it heard instead." },
      { id: "metric", type: "select", label: "Cell shows", options: [["rate", "share of scans that heard it"], ["median_rssi", "median signal"]], value: "rate", show: (p) => p.view === "grid" },
      { id: "min_rssi", type: "range", label: "Weakest signal that counts", min: -100, max: -50, step: 1, value: -90, fmt: (v) => `${v} dBm` },
    ],
    outputs: [
      {
        key: "matrix",
        code: (p, S) =>
          p.view === "network"
            ? `res = dep.proximity_network(${U.str(S.ref(p.center))}, min_rssi=${p.min_rssi})`
            : `res = dep.proximity_matrix(min_rssi=${p.min_rssi}, metric=${U.str(p.metric)})`,
        read(r, p, S) {
          const s = r.summary;
          if (p.view === "network") {
            const c = U.esc(s.center_label);
            const lead = `<p><b>${c}</b> heard <b>${s.n_heard} of ${U.pl(s.n_peers, "other tag")}</b> at ${p.min_rssi} dBm or stronger.`;
            if (!s.best_pair) {
              return `${lead}${s.n_heard ? ` None of them is a ${s.center_role === "mobile" ? "base" : "mobile"}, so there is no mobile-to-base link to point at.` : ""}</p>`;
            }
            const kind = s.center_role === "mobile" ? "base" : "mobile";
            return `${lead} Of the ${kind}s, the strongest link was <b>${U.esc(s.best_pair)}</b> (${U.pct(s.best_value)} of its scans, median ${U.n(s.best_median_rssi, 0)} dBm), outlined in the graph. Nearer the centre means a stronger median signal; wider edges and bigger nodes mean more of its scans heard that tag.</p>`;
          }
          if (s.best_pair) {
            const how = s.best_metric === "rate" ? `${U.pct(s.best_value)} of its scans` : `a median of ${U.n(s.best_value, 0)} dBm`;
            return `<p>Between a mobile and a base, the strongest link was <b>${U.esc(s.best_pair)}</b> (${how}), outlined in the grid. That is the best hint of where a worn tag spent its time, though walls and orientation shape it as much as distance.</p>`;
          }
          return "";
        },
      },
    ],
  },

  /* 7 -------------------------------------------------------------------- */
  {
    id: "together",
    title: "Did two tags spend time together?",
    question:
      "Take two tags and ask when they were near each other. An encounter is a run of sightings, and a tag that only half-heard the other does not break it. pyJuxta also reports whether the hearing was mutual, which is a check on whether the radio, not the animals, is driving the result.",
    tryThis: "Raise the RSSI threshold until only close contact is left, and see how much of the shared time survives. Switch the direction to see whether one tag hears the other more often.",
    needs: (S) => (S.ids.length >= 2 ? null : "This step needs at least two tags."),
    controls: [
      { id: "a", type: "device", label: "First tag", pool: "all", def: (S) => S.mobiles[0] || S.ids[0] },
      { id: "b", type: "device", label: "Second tag", pool: "all", def: (S) => S.mobiles[1] || S.ids.find((i) => i !== (S.mobiles[0] || S.ids[0])) },
      { id: "min_rssi", type: "range", label: "Weakest signal that counts", min: -100, max: -40, step: 1, value: -70, fmt: (v) => `${v} dBm` },
      { id: "merge_gap_s", type: "range", label: "Join sightings closer than", min: 30, max: 600, step: 30, value: 90, fmt: (v) => U.mins(v / 60) },
      { id: "min_detections", type: "range", label: "Sightings needed for an encounter", min: 1, max: 10, step: 1, value: 2, fmt: (v) => `${v}` },
      { id: "direction", type: "select", adv: true, label: "Count a sighting when", options: DIRECTIONS_PAIR, value: "either" },
      { id: "window_s", type: "select", adv: true, label: "Reciprocity window", options: [[60, "1 min"], [120, "2 min"], [300, "5 min"], [600, "10 min"]], value: 120, help: "Each tag's sightings are compared inside windows this long." },
    ],
    outputs: [
      {
        key: "proximity",
        code: (p, S) =>
          `res = dep.proximity(${U.str(S.ref(p.a))}, ${U.str(S.ref(p.b))}, min_rssi=${p.min_rssi}, merge_gap_s=${p.merge_gap_s}, min_detections=${p.min_detections}, direction=${U.str(p.direction)}, window_s=${p.window_s})`,
        read(r, p, S) {
          const s = r.summary;
          const a = U.esc(S.name(s.a)), b = U.esc(S.name(s.b));
          return `<p>Both tags were recording together for <b>${U.n(s.overlap_h, 1)} h</b>. In that time there were <b>${U.pl(s.episodes, "encounter")}</b> adding up to <b>${U.n(s.together_h, 1)} h</b> (${U.pct(s.together_share_of_overlap)} of the shared time), median ${U.mins(s.median_episode_min)}, longest ${U.mins(s.longest_episode_min)}.</p>
            <p>${a} heard ${b} in ${U.pct(s.rate_a_hears_b)} of its scans, and ${b} heard ${a} in ${U.pct(s.rate_b_hears_a)}. When ${a} heard ${b}, ${b} also heard ${a} ${U.pct(s.p_b_hears_a_given_a_hears_b)} of the time (overlap of the two sets: ${U.pct(s.reciprocity_jaccard)}).</p>`;
        },
      },
    ],
  },

  /* 8 -------------------------------------------------------------------- */
  {
    id: "synchrony",
    title: "Do they move together?",
    question:
      "Two animals that are both busy by day will correlate with no social link at all. So pyJuxta does not stop at one correlation: it compares it with shifted copies of the data, removes time of day, and splits the day into together and apart. Read the rows in order and see which parts survive.",
    tryThis:
      "Change the bin width and watch the correlation move. Switch the null from circular shifts to whole-day shifts and see how few days there really are. Shorten the maximum lag to see whether the peak stays at zero.",
    needs: (S) => (S.ids.length >= 2 ? null : "This step needs at least two tags."),
    controls: [
      { id: "a", type: "device", label: "First tag", pool: "all", def: (S) => S.mobiles[0] || S.ids[0] },
      { id: "b", type: "device", label: "Second tag", pool: "all", def: (S) => S.mobiles[1] || S.ids.find((i) => i !== (S.mobiles[0] || S.ids[0])) },
      { id: "bin_s", type: "select", label: "Bin width", options: BIN_OPTS([60, 300, 600, 900]), value: 300, help: "Activity is compared bin by bin." },
      { id: "signal", type: "select", label: "Compare", options: [["motion_mean", "mean motion count"], ["active_frac", "share of minutes active"]], value: "motion_mean" },
      { id: "method", type: "select", label: "Correlation", options: [["spearman", "Spearman (rank)"], ["pearson", "Pearson"]], value: "spearman", help: "Rank correlation is less thrown by a few very busy bins." },
      { id: "max_lag_bins", type: "range", label: "Largest lag to try", min: 0, max: 36, step: 1, value: 12, fmt: (v) => `${v} bins` },
      { id: "null", type: "select", label: "Shift test", options: [["circular", "random circular shifts"], ["day_shift", "whole-day shifts"]], value: "circular", help: "Compare with the same data shifted in time, which keeps each tag's own daily pattern." },
      { id: "n_null", type: "range", label: "Number of shifts", min: 50, max: 1000, step: 50, value: 300, fmt: (v) => `${v}`, help: "More shifts give a finer p-value and take longer." },
      { id: "active_threshold", type: "range", adv: true, label: "A minute counts as active at motion ≥", min: 1, max: 300, step: 1, value: 1, fmt: (v) => `${v}` },
      { id: "transform", type: "select", adv: true, label: "Transform", options: [["none", "none"], ["log1p", "log(1 + x)"]], value: "none" },
      { id: "inpaint", type: "select", adv: true, label: "Fill gaps up to", options: INPAINT_OPTS, value: 0, help: INPAINT_HELP + " Filled bins are estimates, so they can inflate a correlation." },
      { id: "min_shift_h", type: "range", adv: true, label: "Smallest shift", min: 0.5, max: 12, step: 0.5, value: 2, fmt: (v) => `${v} h`, help: "Shifts smaller than this would barely move the data." },
      { id: "min_rssi", type: "range", adv: true, label: "Encounter: weakest signal", min: -100, max: -40, step: 1, value: -70, fmt: (v) => `${v} dBm` },
      { id: "merge_gap_s", type: "range", adv: true, label: "Encounter: join sightings closer than", min: 30, max: 600, step: 30, value: 90, fmt: (v) => U.mins(v / 60) },
      { id: "seed", type: "number", adv: true, label: "Random seed", min: 0, max: 99999, value: 0, help: "The same seed gives the same shifts." },
    ],
    outputs: [
      {
        key: "synchrony",
        code: (p, S) =>
          `res = dep.synchrony(${U.str(S.ref(p.a))}, ${U.str(S.ref(p.b))}, bin_s=${p.bin_s}, signal=${U.str(p.signal)}, method=${U.str(p.method)}, max_lag_bins=${p.max_lag_bins},\n                    null=${U.str(p.null)}, n_null=${p.n_null}, active_threshold=${p.active_threshold}, transform=${U.str(p.transform)},\n                    min_shift_h=${p.min_shift_h}, min_rssi=${p.min_rssi}, merge_gap_s=${p.merge_gap_s}, inpaint=${pyInpaint(p.inpaint)}, seed=${p.seed})`,
        read(r, p, S) {
          const s = r.summary;
          const a = U.esc(S.name(s.a)), b = U.esc(S.name(s.b));
          const out = [];
          out.push(`<li><b>The raw number.</b> Over ${U.n(s.aligned_h, 1)} h with both tags recording, ${p.method === "spearman" ? "Spearman" : "Pearson"} correlation was <b>${U.n(s.r, 2)}</b>; the two agreed on active versus quiet in ${U.pct(s.agreement)} of bins (kappa ${U.n(s.kappa, 2)}).</li>`);
          if (U.has(s.null_mean)) {
            const floor = 1 / (s.null_n + 1);
            const pTxt = s.p_shift <= floor + 1e-9 ? `p ≤ ${U.n(floor, 3)} (the smallest ${s.null_n} shifts can give)` : `p = ${U.n(s.p_shift, 3)}`;
            out.push(`<li><b>Against shifted copies.</b> Shifting ${b} in time ${U.pl(s.null_n, "time")} gave a correlation of ${U.n(s.null_mean, 2)} on average (spread ${U.n(s.null_sd, 2)}), so the data beat chance by <b>${U.n(s.excess_r, 2)}</b>; ${pTxt}.</li>`);
          } else {
            out.push(`<li><b>Against shifted copies.</b> Not computed: the data do not allow enough shifts of at least ${p.min_shift_h} h.</li>`);
          }
          if (U.has(s.r_time_of_day_removed)) {
            out.push(`<li><b>With time of day removed.</b> Subtracting each tag's own average for that hour of the day leaves a correlation of <b>${U.n(s.r_time_of_day_removed, 2)}</b>. This is the part not explained by both being busy at the same hours.</li>`);
          } else {
            out.push(`<li><b>With time of day removed.</b> Needs at least 3 local days of data; this dataset has ${s.local_days_aligned}.</li>`);
          }
          if (U.has(s.r_together)) {
            const tail = U.has(s.p_together_vs_matched) ? `; p = ${U.n(s.p_together_vs_matched, 3)}` : "";
            out.push(`<li><b>Together versus apart.</b> During ${s.n_together} bins when they were together, r = <b>${U.n(s.r_together, 2)}</b>. Apart (${s.n_apart} bins) it was ${U.n(s.r_apart, 2)}, and ${U.n(s.matched_apart_mean, 2)} when apart bins were drawn from the same hours of day${tail}.</li>`);
          }
          out.push(`<li><b>Lag.</b> The strongest correlation was at ${U.n(s.peak_lag_min, 0)} min (r = ${U.n(s.peak_lag_r, 2)}); a positive lag means ${b} follows ${a}.</li>`);
          return `<ol class="steps">${out.join("")}</ol>`;
        },
        tables: [
          {
            key: "conditioned",
            title: "Together and apart",
            cols: [["windows", "Windows"], ["n_bins", "Bins"], ["r", "r", 2], ["agreement", "Agreement", "pct"], ["kappa", "Kappa", 2]],
          },
        ],
      },
    ],
  },
];
