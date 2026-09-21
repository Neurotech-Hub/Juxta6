/* CSV ingest, merge, and validation, plus shared timezone helpers.
 * Juxta6 schema jxta-nor-csv-v9: JXV/JXB use day-relative `sec` (date in
 * filename); JXV includes BME688 temp_c + humidity; JXS keeps absolute `unix`
 * plus optional latitude/longitude. Downstream plots/summary use unix. */

const TZ_ABBR = {
  "America/Chicago": "CT",
  "America/New_York": "ET",
  "UTC": "UTC",
};

// Returns "YYYY-MM-DD HH:mm:ss" in the given timezone — the string format
// Plotly treats as a plain (timezone-less) date, so axes render in local time.
function unixToTzString(unix, tz) {
  const parts = new Intl.DateTimeFormat("en-CA", {
    timeZone: tz,
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit",
    hour12: false,
  }).formatToParts(new Date(unix * 1000));
  const p = {};
  for (const { type, value } of parts) p[type] = value;
  // en-CA with hour12:false can emit "24" for midnight
  const hour = p.hour === "24" ? "00" : p.hour;
  return `${p.year}-${p.month}-${p.day} ${hour}:${p.minute}:${p.second}`;
}

// Human-readable datetime, e.g. "Jul 9, 2026, 2:19:34 PM"
function formatUnix(unix, tz, opts) {
  return new Intl.DateTimeFormat("en-US", Object.assign({
    timeZone: tz,
    month: "short", day: "numeric", year: "numeric",
    hour: "numeric", minute: "2-digit", second: "2-digit",
  }, opts)).format(new Date(unix * 1000));
}

function classifyFile(filename) {
  const base = filename.split("/").pop().toUpperCase();
  if (base.startsWith("JXV")) return "jxv";
  if (base.startsWith("JXB")) return "jxb";
  if (base.startsWith("JXS")) return "jxs";
  return null;
}

/** Extract YYYYMMDD from JXV20260507.csv (or path ending in that). */
function dateKeyFromFilename(filename) {
  const base = filename.split("/").pop();
  const m = /^JX[SVB](\d{8})\.csv$/i.exec(base);
  return m ? m[1] : null;
}

/** UTC midnight unix for YYYYMMDD. */
function utcMidnightUnix(dateKey) {
  if (!/^\d{8}$/.test(dateKey)) return null;
  const y = Number(dateKey.slice(0, 4));
  const mo = Number(dateKey.slice(4, 6));
  const d = Number(dateKey.slice(6, 8));
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return null;
  return Math.floor(Date.UTC(y, mo - 1, d, 0, 0, 0) / 1000);
}

function parseCsvFile(file) {
  return new Promise((resolve) => {
    const reader = new FileReader();
    reader.onload = () => {
      let text = String(reader.result || "");
      // Raw NOR dumps may lead with a bare filename line before the header.
      const firstNl = text.indexOf("\n");
      if (firstNl > 0) {
        const first = text.slice(0, firstNl).trim();
        if (/^JX[SVB]\d{8}\.csv$/i.test(first)) {
          text = text.slice(firstNl + 1);
        }
      }
      Papa.parse(text, {
        header: true,
        skipEmptyLines: true,
        dynamicTyping: false,
        complete: (results) => resolve(results.data),
        error: () => resolve([]),
      });
    };
    reader.onerror = () => resolve([]);
    reader.readAsText(file);
  });
}

function coerceNumeric(rows, numericCols) {
  return rows.map((r) => {
    const out = Object.assign({}, r);
    for (const col of numericCols) out[col] = Number(r[col]);
    return out;
  });
}

/**
 * JXV/JXB v6: attach absolute unix from filename dateKey + sec.
 * Returns { rows, dropped }.
 */
function normalizeDayRelative(rows, dateKey, numericCols) {
  const midnight = utcMidnightUnix(dateKey);
  if (midnight == null) {
    return { rows: [], dropped: rows.length };
  }
  let dropped = 0;
  const out = [];
  for (const r of coerceNumeric(rows, ["sec", ...numericCols])) {
    if (!Number.isFinite(r.sec) || r.sec < 0 || r.sec >= 86400) {
      dropped += 1;
      continue;
    }
    const row = Object.assign({}, r, { unix: midnight + Math.floor(r.sec) });
    out.push(row);
  }
  return { rows: out, dropped };
}

// Parses a FileList/array of File objects into merged, sorted datasets.
// Returns { jxv, jxb, jxs, fileCounts, warnings, error, deviceId }
async function ingestFiles(files) {
  const data = { jxv: [], jxb: [], jxs: [] };
  const fileCounts = { jxv: 0, jxb: 0, jxs: 0 };
  const warnings = [];

  for (const file of files) {
    const type = classifyFile(file.name);
    if (!type) {
      warnings.push(`Ignored "${file.name}" — filename must start with JXV, JXB, or JXS.`);
      continue;
    }

    const rows = await parseCsvFile(file);
    if (rows.length === 0) {
      warnings.push(`Skipped "${file.name}" — empty or unparseable.`);
      continue;
    }

    if (type === "jxs") {
      const coerced = coerceNumeric(rows, ["unix"])
        .filter((r) => Number.isFinite(r.unix) && r.unix > 0);
      if (coerced.length === 0) {
        warnings.push(`Skipped "${file.name}" — no valid unix timestamps.`);
        continue;
      }
      fileCounts.jxs += 1;
      data.jxs.push(...coerced);
      continue;
    }

    // JXV / JXB — day-relative sec
    const dateKey = dateKeyFromFilename(file.name);
    if (!dateKey) {
      warnings.push(
        `Skipped "${file.name}" — expected JX{S|V|B}YYYYMMDD.csv for schema v9.`
      );
      continue;
    }

    const numericCols = type === "jxv"
      ? ["motion", "batt_v", "temp_c", "humidity"]
      : ["rssi"];
    const { rows: normalized, dropped } = normalizeDayRelative(rows, dateKey, numericCols);
    if (dropped > 0) {
      warnings.push(`Dropped ${dropped} row(s) in "${file.name}" (invalid sec).`);
    }
    if (normalized.length === 0) {
      warnings.push(`Skipped "${file.name}" — no valid sec rows.`);
      continue;
    }
    fileCounts[type] += 1;
    data[type].push(...normalized);
  }

  for (const type of ["jxv", "jxb", "jxs"]) {
    data[type].sort((a, b) => a.unix - b.unix);
  }

  // Single-device validation from JXS device_id only (v6 has no observer column).
  const ids = new Set();
  for (const r of data.jxs) if (r.device_id) ids.add(r.device_id);

  let error = null;
  if (ids.size > 1) {
    error = `Files span multiple devices (${[...ids].sort().join(", ")}). ` +
      `Please upload files from a single device at a time.`;
  }

  const deviceId = data.jxs.find((r) => r.device_id)?.device_id || null;

  return { jxv: data.jxv, jxb: data.jxb, jxs: data.jxs, fileCounts, warnings, error, deviceId };
}
