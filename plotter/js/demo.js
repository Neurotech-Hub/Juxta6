/* Synthetic jxta-nor-csv-v9 package for juxta6-0-prod demos (offline-safe). */

(function (global) {
  const DEMO_DATE = "20260921";
  const DEVICE = "JX_A1B2C3";
  const FW = "6.5.0";
  const LAT = "38.635883";
  const LON = "-90.255356";
  /* UTC midnight 2026-09-21 */
  const DAY_MIDNIGHT_UNIX = Math.floor(Date.UTC(2026, 8, 21) / 1000);
  const PROD_START_SEC = 14 * 3600; /* 14:00 UTC day-relative */
  const HOURS = 3;
  const VITALS_INTERVAL_S = 60;
  const SCAN_INTERVAL_S = 30;

  function jxsRow(unix, event, loc) {
    const lat = loc ? LAT : "";
    const lon = loc ? LON : "";
    return [
      unix,
      event,
      DEVICE,
      DEVICE,
      "demo-trial",
      FW,
      "30",
      "2",
      "60",
      DEVICE,
      lat,
      lon,
    ].join(",");
  }

  function buildJxs() {
    const syncUnix = DAY_MIDNIGHT_UNIX + PROD_START_SEC - 8;
    const lines = [
      "unix,event,device_id,subject_id,experiment,fw_version,scan_interval_s,adv_interval_s,vitals_interval_s,ble_name,latitude,longitude",
      jxsRow(DAY_MIDNIGHT_UNIX, "day_start", true),
      jxsRow(syncUnix, "shelf_exit", false),
      jxsRow(syncUnix, "boot", false),
      jxsRow(syncUnix, "user_connected", false),
      jxsRow(syncUnix, "time_set", true),
      jxsRow(syncUnix + 8, "user_disconnected", false),
      jxsRow(syncUnix + 8, "boot", false),
    ];
    const endUnix = DAY_MIDNIGHT_UNIX + PROD_START_SEC + HOURS * 3600 + 120;
    lines.push(jxsRow(endUnix, "shelf_entry", false));
    return lines.join("\n") + "\n";
  }

  function buildJxv() {
    const lines = ["sec,motion,batt_v,temp_c,humidity"];
    const n = (HOURS * 3600) / VITALS_INTERVAL_S;
    for (let i = 0; i < n; i++) {
      const sec = PROD_START_SEC + i * VITALS_INTERVAL_S;
      const t = i / Math.max(1, n - 1);
      const motion = i % 17 === 0 ? 3 + (i % 5) : i % 7 === 0 ? 1 : 0;
      const batt = (2.95 - 0.12 * t).toFixed(2);
      const temp = (22.4 + 1.8 * Math.sin(t * Math.PI * 2) + 0.3 * t).toFixed(1);
      const humidity = (41.0 + 6.0 * Math.sin(t * Math.PI * 2 + 0.8) - 1.5 * t).toFixed(1);
      lines.push([sec, motion, batt, temp, humidity].join(","));
    }
    return lines.join("\n") + "\n";
  }

  function buildJxb() {
    /* Storage form: X|B + 6 hex (plotter reconstructs JX_/JB_ for display). */
    const peers = ["XAABBCC", "XDDEEFF", "B112233"];
    const lines = ["sec,peer_id,rssi"];
    const end = PROD_START_SEC + HOURS * 3600;
    for (let sec = PROD_START_SEC + 15; sec < end; sec += SCAN_INTERVAL_S) {
      for (let p = 0; p < peers.length; p++) {
        if ((sec / SCAN_INTERVAL_S + p) % 3 === 0) {
          continue; /* occasional miss */
        }
        const base = -62 - p * 8;
        const wobble = ((sec / SCAN_INTERVAL_S + p * 5) % 11) - 5;
        const rssi = Math.max(-95, Math.min(-45, base + wobble));
        lines.push([sec, peers[p], rssi].join(","));
      }
    }
    return lines.join("\n") + "\n";
  }

  function toFile(name, text) {
    return new File([text], name, { type: "text/csv" });
  }

  global.buildJuxtaDemoFiles = function buildJuxtaDemoFiles() {
    return [
      toFile(`JXS${DEMO_DATE}.csv`, buildJxs()),
      toFile(`JXV${DEMO_DATE}.csv`, buildJxv()),
      toFile(`JXB${DEMO_DATE}.csv`, buildJxb()),
    ];
  };
})(typeof window !== "undefined" ? window : globalThis);
