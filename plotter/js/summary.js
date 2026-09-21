/* Compact summary cards + shareable plain-text copy. */

function formatDuration(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.round((seconds % 3600) / 60);
  return h > 0 ? `${h}h ${m}m` : `${m}m`;
}

function parseCoord(value) {
  if (value === undefined || value === null || value === "") return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

/** Latest JXS row with both latitude and longitude. */
function findLastLocation(jxs) {
  if (!jxs || jxs.length === 0) return null;
  let best = null;
  for (const r of jxs) {
    const lat = parseCoord(r.latitude);
    const lon = parseCoord(r.longitude);
    if (lat == null || lon == null) continue;
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) continue;
    if (!best || r.unix >= best.unix) {
      best = { lat, lon, unix: r.unix, event: r.event || "" };
    }
  }
  return best;
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function collectSummaryMetrics(data, tz) {
  const { jxv, jxb, jxs, deviceId } = data;
  const abbr = TZ_ABBR[tz];
  const cards = [];

  const fw = [...jxs].reverse().find((r) => r.fw_version)?.fw_version;
  const id = deviceId || [...jxs].find((r) => r.device_id)?.device_id || null;
  if (id || fw) {
    const idLine = id || "—";
    const fwLine = fw || "—";
    const isBase = id && String(id).startsWith("JB_");
    const typeLine = id ? (isBase ? "Base Station" : "Mobile") : "—";
    cards.push({
      label: "Device",
      value: "",
      copyLines: [
        id ? `ID: ${id}` : null,
        id ? `Type: ${typeLine}` : null,
        fw ? `Firmware: ${fw}` : null,
      ].filter(Boolean),
      bodyHtml: `
        <div class="metric-row"><span class="metric-k">ID</span><span class="metric-v">${escapeHtml(idLine)}</span></div>
        <div class="metric-row"><span class="metric-k">Type</span><span class="metric-v">${escapeHtml(typeLine)}</span></div>
        <div class="metric-row"><span class="metric-k">Firmware</span><span class="metric-v">${escapeHtml(fwLine)}</span></div>`,
    });
  }

  const allUnix = [...jxv, ...jxb].map((r) => r.unix);
  if (allUnix.length > 0) {
    const start = Math.min(...allUnix);
    const end = Math.max(...allUnix);
    const startStr = `${formatUnix(start, tz, { second: undefined })} ${abbr}`;
    const endStr = `${formatUnix(end, tz, { second: undefined })} ${abbr}`;
    cards.push({
      label: "Duration",
      value: formatDuration(end - start),
      copyLines: [`Duration: ${formatDuration(end - start)}`, `Start: ${startStr}`, `End: ${endStr}`],
      bodyHtml: `
        <div class="metric-time metric-time-start">${escapeHtml(startStr)}</div>
        <div class="metric-time metric-time-end">${escapeHtml(endStr)}</div>`,
    });
  }

  const temps = jxv.map((r) => r.temp_c).filter((t) => Number.isFinite(t));
  const hums = jxv.map((r) => r.humidity).filter((h) => Number.isFinite(h));
  if (temps.length > 0 || hums.length > 0) {
    const tempLine =
      temps.length > 0
        ? `${Math.min(...temps).toFixed(1)}–${Math.max(...temps).toFixed(1)} °C`
        : "—";
    const humLine =
      hums.length > 0
        ? `${Math.min(...hums).toFixed(1)}–${Math.max(...hums).toFixed(1)} %`
        : "—";
    cards.push({
      label: "Environment",
      value: "",
      copyLines: [
        temps.length > 0 ? `Temp: ${tempLine}` : null,
        hums.length > 0 ? `Humidity: ${humLine}` : null,
      ].filter(Boolean),
      bodyHtml: `
        <div class="metric-row"><span class="metric-k">Temp</span><span class="metric-v">${escapeHtml(tempLine)}</span></div>
        <div class="metric-row"><span class="metric-k">Humidity</span><span class="metric-v">${escapeHtml(humLine)}</span></div>`,
    });
  }

  const motions = jxv.map((r) => r.motion).filter((m) => Number.isFinite(m));
  const peerIds = new Set(jxb.map((r) => r.peer_id).filter(Boolean));
  if (motions.length > 0 || peerIds.size > 0) {
    let motionMain = "—";
    if (motions.length > 0) {
      const activePct = (100 * motions.filter((m) => m > 0).length) / motions.length;
      motionMain = `${activePct.toFixed(1)}% Active`;
    }
    const peerMain = peerIds.size > 0 ? String(peerIds.size) : "—";
    cards.push({
      label: "Action",
      value: "",
      copyLines: [
        motions.length > 0 ? `Motion: ${motionMain}` : null,
        peerIds.size > 0 ? `Peers: ${peerMain}` : null,
      ].filter(Boolean),
      bodyHtml: `
        <div class="metric-row"><span class="metric-k">Motion</span><span class="metric-v">${escapeHtml(motionMain)}</span></div>
        <div class="metric-row"><span class="metric-k">Peers</span><span class="metric-v">${escapeHtml(peerMain)}</span></div>`,
    });
  }

  return cards;
}

function buildSummaryText(data, tz) {
  return collectSummaryMetrics(data, tz)
    .flatMap((c) => c.copyLines || [])
    .join("\n");
}

function renderSummary(data, tz) {
  const grid = document.getElementById("summary-grid");
  const hero = document.getElementById("summary-hero");
  const heroImg = document.getElementById("summary-hero-img");
  const deviceId =
    data.deviceId || data.jxs?.find((r) => r.device_id)?.device_id || null;

  if (hero && heroImg) {
    const isBase = deviceId && String(deviceId).startsWith("JB_");
    heroImg.src = isBase
      ? "assets/juxta_tag_base.png"
      : "assets/juxta_tag_mobile.png";
    heroImg.alt = isBase ? "Juxta base station" : "Juxta mobile tag";
    hero.hidden = false;
  }

  const cards = collectSummaryMetrics(data, tz);
  if (cards.length === 0) {
    grid.innerHTML = '<p class="empty-note">No summary metrics in this package.</p>';
    return;
  }
  grid.innerHTML = cards
    .map((c) => {
      const valueHtml = c.value
        ? `<div class="metric-value">${escapeHtml(c.value)}</div>`
        : "";
      return `
    <div class="metric-card">
      <div class="metric-label">${escapeHtml(c.label)}</div>
      ${valueHtml}
      ${c.bodyHtml || ""}
    </div>`;
    })
    .join("");
}

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    const ta = document.createElement("textarea");
    ta.value = text;
    ta.style.position = "fixed";
    ta.style.opacity = "0";
    document.body.appendChild(ta);
    ta.select();
    let ok = false;
    try {
      ok = document.execCommand("copy");
    } catch {
      /* ignore */
    }
    ta.remove();
    return ok;
  }
}

function initCopyButton(getState) {
  const btn = document.getElementById("copy-summary-btn");
  btn.addEventListener("click", async () => {
    const { data, tz } = getState();
    if (!data) return;
    const ok = await copyText(buildSummaryText(data, tz));
    btn.textContent = ok ? "Copied!" : "Copy failed";
    if (ok) btn.classList.add("copied");
    setTimeout(() => {
      btn.textContent = "Copy summary";
      btn.classList.remove("copied");
    }, 2000);
  });
}
