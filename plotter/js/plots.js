/* Plotly charts for JXV vitals and JXB peer timeline. */

const PLOT_CONFIG = { responsive: true, displaylogo: false };

// Identical margins on every time plot keep the plot areas pixel-aligned;
// the right margin reserves room for the RSSI colorbar / right y-axis.
const BASE_LAYOUT = {
  margin: { l: 90, r: 130, t: 10, b: 45 },
  font: { family: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, sans-serif", size: 12 },
  paper_bgcolor: "rgba(0,0,0,0)",
  plot_bgcolor: "rgba(0,0,0,0)",
};

function showEmpty(elId, message) {
  const el = document.getElementById(elId);
  el.innerHTML = `<p class="empty-note">${message}</p>`;
}

// Shared x-axis range across JXV and JXB so all time plots align.
// Returns [minStr, maxStr] in the given timezone, or null if no data.
function sharedTimeRange(jxv, jxb, tz) {
  const allUnix = [...jxv, ...jxb].map((r) => r.unix);
  if (allUnix.length === 0) return null;
  return [
    unixToTzString(Math.min(...allUnix), tz),
    unixToTzString(Math.max(...allUnix), tz),
  ];
}

function xAxis(abbr, range) {
  const axis = { title: { text: `Time (${abbr})` }, type: "date" };
  if (range) axis.range = range;
  return axis;
}

function renderActivityPlot(jxv, tz, range) {
  if (jxv.length === 0) {
    showEmpty("plot-activity", "No JXV files loaded.");
    return;
  }
  const x = jxv.map((r) => unixToTzString(r.unix, tz));
  const abbr = TZ_ABBR[tz];

  Plotly.newPlot("plot-activity", [{
    x,
    y: jxv.map((r) => r.motion),
    type: "scatter",
    mode: "lines",
    line: { color: "#2563eb", width: 1.5 },
    name: "Motion",
    hovertemplate: "%{x}<br>Motion: %{y}<extra></extra>",
  }], Object.assign({}, BASE_LAYOUT, {
    height: 260,
    xaxis: xAxis(abbr, range),
    yaxis: { title: { text: "Motion count" }, rangemode: "tozero" },
    showlegend: false,
  }), PLOT_CONFIG);
}

function renderBattTempPlot(jxv, tz, range) {
  if (jxv.length === 0) {
    showEmpty("plot-batt-temp", "No JXV files loaded.");
    return;
  }
  const x = jxv.map((r) => unixToTzString(r.unix, tz));
  const abbr = TZ_ABBR[tz];

  Plotly.newPlot("plot-batt-temp", [
    {
      x,
      y: jxv.map((r) => r.batt_v),
      type: "scatter",
      mode: "lines",
      line: { color: "#16a34a", width: 1.5 },
      name: "Battery (V)",
      hovertemplate: "%{x}<br>Battery: %{y:.2f} V<extra></extra>",
    },
    {
      x,
      y: jxv.map((r) => r.temp_c),
      type: "scatter",
      mode: "lines",
      line: { color: "#dc6b26", width: 1.5 },
      name: "Temp (°C)",
      yaxis: "y2",
      hovertemplate: "%{x}<br>Temp: %{y} °C<extra></extra>",
    },
  ], Object.assign({}, BASE_LAYOUT, {
    height: 280,
    xaxis: xAxis(abbr, range),
    yaxis: { title: { text: "Battery (V)" }, tickformat: ".2f" },
    yaxis2: { title: { text: "Temperature (°C)" }, overlaying: "y", side: "right" },
    legend: { orientation: "h", y: 1.12 },
  }), PLOT_CONFIG);
}

function renderPeersPlot(jxb, tz, range) {
  if (jxb.length === 0) {
    showEmpty("plot-peers", "No JXB files loaded.");
    return;
  }
  const abbr = TZ_ABBR[tz];
  const peers = [...new Set(jxb.map((r) => r.peer_id))].sort().reverse(); // reverse so A-Z reads top-down

  Plotly.newPlot("plot-peers", [{
    x: jxb.map((r) => unixToTzString(r.unix, tz)),
    y: jxb.map((r) => r.peer_id),
    type: "scatter",
    mode: "markers",
    marker: {
      size: 8,
      color: jxb.map((r) => r.rssi),
      colorscale: [[0, "#d73027"], [0.5, "#fee08b"], [1, "#1a9850"]],
      cmin: -95,
      cmax: -55,
      colorbar: { title: { text: "RSSI (dBm)" }, thickness: 14 },
    },
    customdata: jxb.map((r) => r.rssi),
    hovertemplate: "%{y}<br>%{x}<br>RSSI: %{customdata} dBm<extra></extra>",
  }], Object.assign({}, BASE_LAYOUT, {
    height: Math.max(220, 80 + peers.length * 40),
    xaxis: xAxis(abbr, range),
    yaxis: { title: { text: "Peer" }, type: "category", categoryorder: "array", categoryarray: peers },
    showlegend: false,
  }), PLOT_CONFIG);
}

function renderAllPlots(data, tz) {
  const range = sharedTimeRange(data.jxv, data.jxb, tz);
  renderActivityPlot(data.jxv, tz, range);
  renderBattTempPlot(data.jxv, tz, range);
  renderPeersPlot(data.jxb, tz, range);
}
