/* JXS system events rendered as a compact table with timezone-converted times. */

function renderEventsTable(jxs, tz) {
  const container = document.getElementById("events-table");
  if (jxs.length === 0) {
    container.innerHTML = '<p class="empty-note">No JXS files loaded.</p>';
    return;
  }
  const abbr = TZ_ABBR[tz];

  const rowsHtml = jxs.map((r) => `
    <tr>
      <td>${formatUnix(r.unix, tz)}</td>
      <td>${r.event || ""}</td>
      <td>${r.device_id || ""}</td>
      <td>${r.fw_version || ""}</td>
      <td>${r.scan_interval_s || ""} / ${r.adv_interval_s || ""} / ${r.vitals_interval_s || ""}</td>
    </tr>`).join("");

  container.innerHTML = `
    <table class="events">
      <thead>
        <tr>
          <th>Time (${abbr})</th>
          <th>Event</th>
          <th>Device</th>
          <th>FW</th>
          <th>Intervals (scan/adv/vitals, s)</th>
        </tr>
      </thead>
      <tbody>${rowsHtml}</tbody>
    </table>`;
}
