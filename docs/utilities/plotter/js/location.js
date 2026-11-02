/* Last Location: Leaflet map for latest JXS lat/lon. */

(function (global) {
  let map = null;
  let marker = null;
  let tileLayer = null;

  function ensureMap() {
    if (map) return map;
    const el = document.getElementById("location-map");
    if (!el || typeof L === "undefined") return null;

    map = L.map(el, {
      zoomControl: true,
      attributionControl: true,
      scrollWheelZoom: false,
    });
    // Esri Canvas Dark Gray — no API key for typical low-volume use.
    // (OSM.org and CARTO basemap CDNs block/key-gate this class of web app.)
    tileLayer = L.tileLayer(
      "https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/{z}/{y}/{x}",
      {
        maxZoom: 16,
        attribution:
          "Tiles &copy; Esri &mdash; Esri, DeLorme, NAVTEQ",
      }
    ).addTo(map);
    L.tileLayer(
      "https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Reference/MapServer/tile/{z}/{y}/{x}",
      {
        maxZoom: 16,
        attribution: "",
      }
    ).addTo(map);
    return map;
  }

  function clearMarker() {
    if (map && marker) {
      map.removeLayer(marker);
      marker = null;
    }
  }

  global.renderLastLocation = function renderLastLocation(data, tz) {
    const section = document.getElementById("location-section");
    const meta = document.getElementById("location-meta");
    const empty = document.getElementById("location-empty");
    const mapEl = document.getElementById("location-map");
    if (!section || !meta || !empty || !mapEl) return;

    const loc = findLastLocation(data && data.jxs);
    if (!loc) {
      section.hidden = false;
      mapEl.hidden = true;
      empty.hidden = false;
      meta.textContent = "";
      clearMarker();
      return;
    }

    empty.hidden = true;
    mapEl.hidden = false;
    section.hidden = false;

    const when = formatUnix(loc.unix, tz);
    const abbr = TZ_ABBR[tz];
    const eventBit = loc.event ? ` · ${loc.event}` : "";
    meta.textContent = `${loc.lat.toFixed(6)}, ${loc.lon.toFixed(6)} · ${when} ${abbr}${eventBit}`;

    const m = ensureMap();
    if (!m) {
      empty.hidden = false;
      empty.textContent = "Map library unavailable.";
      mapEl.hidden = true;
      return;
    }

    clearMarker();
    marker = L.marker([loc.lat, loc.lon]).addTo(m);
    m.setView([loc.lat, loc.lon], 16);
    // Leaflet needs a layout pass after the section was unhidden.
    requestAnimationFrame(() => {
      m.invalidateSize();
      m.setView([loc.lat, loc.lon], 16);
    });
  };
})(typeof window !== "undefined" ? window : globalThis);
