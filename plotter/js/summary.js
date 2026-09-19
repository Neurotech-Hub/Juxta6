/* Experiment summary: builds shareable plain text shown on screen and copied to clipboard. */

function formatDuration(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.round((seconds % 3600) / 60);
  return h > 0 ? `${h}h ${m}m` : `${m}m`;
}

function buildSummaryText(data, tz) {
  const { jxv, jxb, jxs, deviceId } = data;
  const abbr = TZ_ABBR[tz];
  const lines = [];

  lines.push(`Device: ${deviceId || "unknown"}`);

  // Duration matches the plots' x-axis timeline (JXV + JXB only)
  const allUnix = [...jxv, ...jxb].map((r) => r.unix);
  if (allUnix.length > 0) {
    const start = Math.min(...allUnix);
    const end = Math.max(...allUnix);
    const startStr = formatUnix(start, tz, { second: undefined });
    const endStr = formatUnix(end, tz, { second: undefined });
    lines.push(`Duration: ${startStr} – ${endStr} ${abbr} (${formatDuration(end - start)})`);
  }

  if (jxv.length > 0) {
    const first = jxv[0].batt_v;
    const last = jxv[jxv.length - 1].batt_v;
    const delta = last - first;
    const sign = delta >= 0 ? "+" : "−";
    lines.push(`Battery: ${first.toFixed(2)} V → ${last.toFixed(2)} V (${sign}${Math.abs(delta).toFixed(2)} V)`);
  }

  if (jxb.length > 0) {
    const counts = new Map();
    for (const r of jxb) counts.set(r.peer_id, (counts.get(r.peer_id) || 0) + 1);
    const sorted = [...counts.entries()].sort((a, b) => b[1] - a[1]);
    const list = sorted.map(([id, n]) => `${id} (${n})`).join(", ");
    lines.push(`Peers (${sorted.length}): ${list}`);
  }

  const fw = [...jxs].reverse().find((r) => r.fw_version)?.fw_version;
  if (fw) lines.push(`FW: ${fw}`);

  if (jxv.length > 0) {
    const motions = jxv.map((r) => r.motion);
    const total = motions.reduce((a, b) => a + b, 0);
    const peak = Math.max(...motions);
    const activePct = Math.round(100 * motions.filter((m) => m > 0).length / motions.length);
    lines.push(`Motion: total ${total.toLocaleString()}, peak ${peak}, active ${activePct}%`);

    const temps = jxv.map((r) => r.temp_c);
    lines.push(`Temp: ${Math.min(...temps)}–${Math.max(...temps)} °C`);
  }

  if (jxs.length > 0) {
    const counts = new Map();
    for (const r of jxs) counts.set(r.event, (counts.get(r.event) || 0) + 1);
    const list = [...counts.entries()].map(([ev, n]) => `${ev} (${n})`).join(", ");
    lines.push(`Events: ${list}`);
  }

  return lines.join("\n");
}

function renderSummary(data, tz) {
  document.getElementById("summary-text").textContent = buildSummaryText(data, tz);
}

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    // Fallback for contexts where the async clipboard API is unavailable
    const ta = document.createElement("textarea");
    ta.value = text;
    ta.style.position = "fixed";
    ta.style.opacity = "0";
    document.body.appendChild(ta);
    ta.select();
    let ok = false;
    try { ok = document.execCommand("copy"); } catch { /* ignore */ }
    ta.remove();
    return ok;
  }
}

function initCopyButton(getState) {
  const btn = document.getElementById("copy-summary-btn");
  btn.addEventListener("click", async () => {
    const { data, tz } = getState();
    const ok = await copyText(buildSummaryText(data, tz));
    btn.textContent = ok ? "Copied!" : "Copy failed";
    if (ok) btn.classList.add("copied");
    setTimeout(() => {
      btn.textContent = "Copy summary";
      btn.classList.remove("copied");
    }, 2000);
  });
}
