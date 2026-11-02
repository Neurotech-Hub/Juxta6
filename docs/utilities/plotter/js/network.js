/* Weighted peer network: this device at the center, one node per JXB peer.
 *
 *   edge width   = number of sightings (weight)
 *   edge length  = mean RSSI (stronger signal sits closer to the center)
 *   edge / node color = mean RSSI on the same blue -> violet -> magenta scale as BLE Peers
 *   node size    = number of sightings
 *
 * Built from every JXB row loaded, so a multi-day upload is aggregated across days. */

const NETWORK_MAX_PEERS = 40;
// Mean RSSI mapped to the radius: NEAR sits on the inner ring, FAR on the outer ring.
const NETWORK_RSSI_NEAR = -50;
const NETWORK_RSSI_FAR = -95;
const NETWORK_R_NEAR = 1;
const NETWORK_R_FAR = 3;
// Same limits as the BLE Peers colorbar so colors mean the same thing in both plots.
const NETWORK_COLOR_MIN = -95;
const NETWORK_COLOR_MAX = -55;

function hexToRgb(hex) {
  const h = hex.replace("#", "");
  return [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16));
}

/** Color for an RSSI value from the active theme's scale. */
function rssiColor(rssi) {
  const scale = theme().rssiScale;
  const t = Math.min(1, Math.max(0, (rssi - NETWORK_COLOR_MIN) / (NETWORK_COLOR_MAX - NETWORK_COLOR_MIN)));
  for (let i = 1; i < scale.length; i++) {
    const [t1, c1] = scale[i];
    if (t <= t1 || i === scale.length - 1) {
      const [t0, c0] = scale[i - 1];
      const f = t1 === t0 ? 0 : Math.min(1, Math.max(0, (t - t0) / (t1 - t0)));
      const a = hexToRgb(c0);
      const b = hexToRgb(c1);
      const mix = a.map((v, k) => Math.round(v + (b[k] - v) * f));
      return `rgb(${mix[0]},${mix[1]},${mix[2]})`;
    }
  }
  return scale[scale.length - 1][1];
}

/** Aggregate JXB rows per peer label. */
function buildPeerStats(jxb) {
  const byPeer = new Map();
  for (const r of jxb) {
    const label = displayPeerName(r.peer_id);
    if (!label) continue;
    let s = byPeer.get(label);
    if (!s) {
      s = { label, count: 0, rssiN: 0, rssiSum: 0, rssiMin: Infinity, rssiMax: -Infinity, first: r.unix, last: r.unix };
      byPeer.set(label, s);
    }
    s.count += 1;
    if (Number.isFinite(r.rssi)) {
      s.rssiN += 1;
      s.rssiSum += r.rssi;
      s.rssiMin = Math.min(s.rssiMin, r.rssi);
      s.rssiMax = Math.max(s.rssiMax, r.rssi);
    }
    s.first = Math.min(s.first, r.unix);
    s.last = Math.max(s.last, r.unix);
  }
  const peers = [...byPeer.values()];
  for (const p of peers) p.meanRssi = p.rssiN > 0 ? p.rssiSum / p.rssiN : NETWORK_RSSI_FAR;
  // Heaviest first; ties broken by name so the layout is stable.
  peers.sort((a, b) => b.count - a.count || a.label.localeCompare(b.label));
  return peers;
}

function networkRadius(meanRssi) {
  const t = Math.min(1, Math.max(0, (NETWORK_RSSI_NEAR - meanRssi) / (NETWORK_RSSI_NEAR - NETWORK_RSSI_FAR)));
  return NETWORK_R_NEAR + (NETWORK_R_FAR - NETWORK_R_NEAR) * t;
}

function networkLabelPosition(angle) {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  if (c > 0.35) return "middle right";
  if (c < -0.35) return "middle left";
  return s > 0 ? "top center" : "bottom center";
}

function renderNetworkGraph(data, tz) {
  const elId = "plot-network";
  const noteEl = document.getElementById("network-note");
  const jxb = data.jxb || [];
  if (jxb.length === 0) {
    showEmpty(elId, "No JXB files loaded.");
    if (noteEl) noteEl.hidden = true;
    return;
  }

  const allPeers = buildPeerStats(jxb);
  const peers = allPeers.slice(0, NETWORK_MAX_PEERS);
  const maxCount = Math.max(...peers.map((p) => p.count));
  const t = theme();
  const abbr = TZ_ABBR[tz];
  const centerLabel = data.deviceId || "This device";
  const totalSightings = jxb.length;

  // Clockwise from the top.
  const step = (2 * Math.PI) / peers.length;
  const placed = peers.map((p, i) => {
    const angle = Math.PI / 2 - i * step;
    const r = networkRadius(p.meanRssi);
    return Object.assign({}, p, { angle, x: r * Math.cos(angle), y: r * Math.sin(angle) });
  });

  const edgeTraces = placed.map((p) => ({
    x: [0, p.x],
    y: [0, p.y],
    type: "scatter",
    mode: "lines",
    line: { color: rssiColor(p.meanRssi), width: 1.5 + 8 * Math.sqrt(p.count / maxCount) },
    opacity: 0.8,
    hoverinfo: "skip",
    showlegend: false,
  }));

  const hoverText = placed.map((p) => {
    const rssiLine = p.rssiN > 0
      ? `mean ${p.meanRssi.toFixed(1)} dBm (${p.rssiMin} to ${p.rssiMax})`
      : "no RSSI values";
    return `${p.label}<br>${p.count} sighting${p.count === 1 ? "" : "s"}<br>${rssiLine}` +
      `<br>first ${formatUnix(p.first, tz)} ${abbr}<br>last ${formatUnix(p.last, tz)} ${abbr}`;
  });

  const peerNodes = {
    x: placed.map((p) => p.x),
    y: placed.map((p) => p.y),
    type: "scatter",
    mode: "markers+text",
    text: placed.map((p) => `${p.label} (${p.count})`),
    textposition: placed.map((p) => networkLabelPosition(p.angle)),
    textfont: { size: 11, color: t.text },
    marker: {
      size: placed.map((p) => 14 + 22 * Math.sqrt(p.count / maxCount)),
      color: placed.map((p) => rssiColor(p.meanRssi)),
      // Opaque so the edge underneath doesn't show through the node.
      opacity: 1,
      line: { color: t.nodeOutline, width: 1.5 },
    },
    hovertext: hoverText,
    hoverinfo: "text",
    showlegend: false,
  };

  const centerNode = {
    x: [0],
    y: [0],
    type: "scatter",
    mode: "markers+text",
    text: [centerLabel],
    textposition: "bottom center",
    textfont: { size: 12, color: t.text },
    marker: { size: 38, color: "#FFFFFF", opacity: 1, line: { color: BRAND.violet, width: 4 } },
    hovertext: [
      `${centerLabel}<br>${allPeers.length} peer${allPeers.length === 1 ? "" : "s"}<br>${totalSightings} sighting${totalSightings === 1 ? "" : "s"}`,
    ],
    hoverinfo: "text",
    showlegend: false,
  };

  // Reference rings, labelled with the RSSI each radius stands for.
  const ringRadii = [NETWORK_R_NEAR, (NETWORK_R_NEAR + NETWORK_R_FAR) / 2, NETWORK_R_FAR];
  const ringShapes = ringRadii.map((r) => ({
    type: "circle",
    xref: "x",
    yref: "y",
    x0: -r,
    y0: -r,
    x1: r,
    y1: r,
    layer: "below",
    line: { color: t.grid, width: 1, dash: "dot" },
  }));
  const labelAngle = Math.PI / 2 - step / 2; // between two nodes
  const ringAnnotations = ringRadii.map((r) => {
    const rssi = NETWORK_RSSI_NEAR - ((r - NETWORK_R_NEAR) / (NETWORK_R_FAR - NETWORK_R_NEAR)) * (NETWORK_RSSI_NEAR - NETWORK_RSSI_FAR);
    return {
      x: r * Math.cos(labelAngle),
      y: r * Math.sin(labelAngle),
      xref: "x",
      yref: "y",
      text: `${Math.round(rssi)} dBm`,
      showarrow: false,
      font: { size: 9, color: t.text },
      opacity: 0.6,
    };
  });

  const extent = NETWORK_R_FAR * 1.3;
  Plotly.newPlot(elId, [...edgeTraces, peerNodes, centerNode], Object.assign(baseLayout(), {
    height: 560,
    margin: { l: 20, r: 20, t: 10, b: 10 },
    xaxis: { visible: false, range: [-extent, extent], fixedrange: true },
    yaxis: { visible: false, range: [-extent, extent], scaleanchor: "x", scaleratio: 1, fixedrange: true },
    shapes: ringShapes,
    annotations: ringAnnotations,
    showlegend: false,
  }), PLOT_CONFIG);

  if (noteEl) {
    const capNote = allPeers.length > peers.length
      ? ` Showing the ${peers.length} most-seen of ${allPeers.length} peers.`
      : "";
    noteEl.textContent =
      "Edge width and node size = sightings. Edge length and color = mean RSSI (shorter and warmer = stronger)." + capNote;
    noteEl.hidden = false;
  }
}
