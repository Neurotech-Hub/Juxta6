/* Plotly charts for JXV vitals and JXB peer timeline. */

const PLOT_CONFIG = { responsive: true, displaylogo: false };

/** JXB storage is X|B + 6 hex; charts show reconstructed JX_/JB_ ADV names. */
function displayPeerName(peerId) {
  if (!peerId || typeof peerId !== "string") return peerId;
  const p = peerId.trim();
  if (p.startsWith("JX_") || p.startsWith("JB_")) return p;
  if (p.length === 7) {
    const role = p[0];
    const hex = p.slice(1);
    if (role === "X" || role === "x") return `JX_${hex}`;
    if (role === "B" || role === "b") return `JB_${hex}`;
  }
  if (p.length === 6) return `JX_${p}`;
  return p;
}

// Brand palette for data series (brand/JUXTA_BRAND.md §3). Blue/violet carry the
// primary signals; magenta is reserved for high-value emphasis (temp, strong RSSI).
const BRAND = {
  navy: "#0B0F3B",
  blue: "#2563EB",
  violet: "#8B5CF6",
  magenta: "#EC0DD9",
  // Lighter blue tint so a thin line stays legible on navy.
  blueLight: "#5B8DEF",
};

// Screen is dark navy; print swaps to a light theme via setPlotTheme().
const PLOT_THEMES = {
  dark: {
    text: "rgba(255,255,255,0.72)",
    grid: "rgba(255,255,255,0.08)",
    axis: "rgba(255,255,255,0.20)",
    series: { motion: BRAND.violet, battery: BRAND.blueLight, temp: BRAND.magenta, humidity: BRAND.blueLight },
  },
  light: {
    text: BRAND.navy,
    grid: "#E2E5EA",
    axis: "#C4C8D0",
    series: { motion: BRAND.violet, battery: BRAND.blue, temp: BRAND.magenta, humidity: BRAND.blue },
  },
};

let activePlotTheme = "dark";

function setPlotTheme(name) {
  if (PLOT_THEMES[name]) activePlotTheme = name;
}

function theme() {
  return PLOT_THEMES[activePlotTheme];
}

// Identical margins on every time plot keep the plot areas pixel-aligned;
// the right margin reserves room for the RSSI colorbar / right y-axis.
function baseLayout() {
  const t = theme();
  return {
    margin: { l: 90, r: 130, t: 10, b: 45 },
    font: { family: "Plus Jakarta Sans, Inter, Helvetica Neue, Arial, sans-serif", size: 12, color: t.text },
    paper_bgcolor: "rgba(0,0,0,0)",
    plot_bgcolor: "rgba(0,0,0,0)",
    hoverlabel: { font: { family: "Plus Jakarta Sans, Inter, Arial, sans-serif" } },
  };
}

// Axis styling shared by every plot; merged with per-plot axis options.
function styledAxis(extra) {
  const t = theme();
  return Object.assign({
    gridcolor: t.grid,
    linecolor: t.axis,
    zerolinecolor: t.grid,
    tickcolor: t.axis,
  }, extra);
}

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
  const axis = styledAxis({ title: { text: `Time (${abbr})` }, type: "date" });
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
    line: { color: theme().series.motion, width: 1.75 },
    name: "Motion",
    hovertemplate: "%{x}<br>Motion: %{y}<extra></extra>",
  }], Object.assign(baseLayout(), {
    height: 260,
    xaxis: xAxis(abbr, range),
    yaxis: styledAxis({ title: { text: "Motion count" }, rangemode: "tozero" }),
    showlegend: false,
  }), PLOT_CONFIG);
}

function renderBatteryPlot(jxv, tz, range) {
  if (jxv.length === 0) {
    showEmpty("plot-battery", "No JXV files loaded.");
    return;
  }
  const x = jxv.map((r) => unixToTzString(r.unix, tz));
  const abbr = TZ_ABBR[tz];

  Plotly.newPlot("plot-battery", [{
    x,
    y: jxv.map((r) => r.batt_v),
    type: "scatter",
    mode: "lines",
    line: { color: theme().series.battery, width: 1.75 },
    name: "Battery (V)",
    hovertemplate: "%{x}<br>Battery: %{y:.2f} V<extra></extra>",
  }], Object.assign(baseLayout(), {
    height: 260,
    xaxis: xAxis(abbr, range),
    yaxis: styledAxis({ title: { text: "Battery (V)" }, tickformat: ".2f" }),
    showlegend: false,
  }), PLOT_CONFIG);
}

function renderTempHumidPlot(jxv, tz, range) {
  if (jxv.length === 0) {
    showEmpty("plot-temp-humid", "No JXV files loaded.");
    return;
  }
  const x = jxv.map((r) => unixToTzString(r.unix, tz));
  const abbr = TZ_ABBR[tz];

  Plotly.newPlot("plot-temp-humid", [
    {
      x,
      y: jxv.map((r) => r.temp_c),
      type: "scatter",
      mode: "lines",
      line: { color: theme().series.temp, width: 1.75 },
      name: "Temp (°C)",
      hovertemplate: "%{x}<br>Temp: %{y:.1f} °C<extra></extra>",
    },
    {
      x,
      y: jxv.map((r) => r.humidity),
      type: "scatter",
      mode: "lines",
      line: { color: theme().series.humidity, width: 1.75 },
      name: "Humidity (%)",
      yaxis: "y2",
      hovertemplate: "%{x}<br>Humidity: %{y:.1f} %<extra></extra>",
    },
  ], Object.assign(baseLayout(), {
    height: 280,
    xaxis: xAxis(abbr, range),
    yaxis: styledAxis({ title: { text: "Temperature (°C)" } }),
    yaxis2: styledAxis({
      title: { text: "Humidity (%)" },
      overlaying: "y",
      side: "right",
      rangemode: "tozero",
      showgrid: false,
    }),
    legend: { orientation: "h", y: 1.12 },
  }), PLOT_CONFIG);
}

function renderPeersPlot(jxb, tz, range) {
  if (jxb.length === 0) {
    showEmpty("plot-peers", "No JXB files loaded.");
    return;
  }
  const abbr = TZ_ABBR[tz];
  const labels = jxb.map((r) => displayPeerName(r.peer_id));
  const peers = [...new Set(labels)].sort().reverse(); // reverse so A-Z reads top-down

  Plotly.newPlot("plot-peers", [{
    x: jxb.map((r) => unixToTzString(r.unix, tz)),
    y: labels,
    type: "scatter",
    mode: "markers",
    marker: {
      size: 8,
      color: jxb.map((r) => r.rssi),
      // Brand gradient as a sequential scale: weak = blue, strong = magenta.
      colorscale: [[0, BRAND.blue], [0.55, BRAND.violet], [1, BRAND.magenta]],
      cmin: -95,
      cmax: -55,
      colorbar: {
        title: { text: "RSSI (dBm)" },
        thickness: 14,
        outlinewidth: 0,
        tickcolor: theme().axis,
      },
    },
    customdata: jxb.map((r) => r.rssi),
    hovertemplate: "%{y}<br>%{x}<br>RSSI: %{customdata} dBm<extra></extra>",
  }], Object.assign(baseLayout(), {
    height: Math.max(220, 80 + peers.length * 40),
    xaxis: xAxis(abbr, range),
    yaxis: styledAxis({
      title: { text: "Peer" },
      type: "category",
      categoryorder: "array",
      categoryarray: peers,
      // Smaller than baseLayout (12) so tick labels clear the "Peer" axis title.
      tickfont: { size: 10 },
    }),
    showlegend: false,
  }), PLOT_CONFIG);
}

function renderAllPlots(data, tz) {
  const range = sharedTimeRange(data.jxv, data.jxb, tz);
  renderActivityPlot(data.jxv, tz, range);
  renderBatteryPlot(data.jxv, tz, range);
  renderTempHumidPlot(data.jxv, tz, range);
  renderPeersPlot(data.jxb, tz, range);
}
