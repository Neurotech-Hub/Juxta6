/* Upload UI and render orchestration. */

(function () {
  let currentData = null;
  let currentTz = document.getElementById("tz-select").value;

  const dropZone = document.getElementById("drop-zone");
  const fileInput = document.getElementById("file-input");
  const loadStatus = document.getElementById("load-status");
  const warningsEl = document.getElementById("warnings");

  function setHidden(id, hidden) {
    document.getElementById(id).hidden = hidden;
  }

  function renderAll() {
    if (!currentData) return;
    // Unhide before rendering so Plotly/Leaflet can measure container width
    setHidden("summary-section", false);
    setHidden("location-section", false);
    setHidden("plots-section", false);
    setHidden("events-section", false);
    renderSummary(currentData, currentTz);
    renderLastLocation(currentData, currentTz);
    renderAllPlots(currentData, currentTz);
    renderEventsTable(currentData.jxs, currentTz);
  }

  function showWarnings(messages) {
    if (messages.length === 0) {
      warningsEl.hidden = true;
      return;
    }
    warningsEl.innerHTML = messages.map((m) => `<p>${m}</p>`).join("");
    warningsEl.hidden = false;
  }

  const isAccepted = (f) => /\.(csv|zip)$/i.test(f.name);

  // "20260507" -> "2026-05-07"
  const formatDayKey = (k) => `${k.slice(0, 4)}-${k.slice(4, 6)}-${k.slice(6, 8)}`;

  async function handleFiles(fileList) {
    const all = [...fileList];
    const files = all.filter(isAccepted);
    const skipped = all.length - files.length;
    if (files.length === 0) {
      showWarnings(["No CSV or ZIP files found in selection."]);
      return;
    }

    const result = await ingestFiles(files);
    const messages = [...result.warnings];
    if (skipped > 0) messages.push(`Ignored ${skipped} file(s) that are not CSV or ZIP.`);

    if (result.error) {
      messages.unshift(result.error);
      showWarnings(messages);
      setHidden("summary-section", true);
      setHidden("location-section", true);
      setHidden("plots-section", true);
      setHidden("events-section", true);
      const hero = document.getElementById("summary-hero");
      if (hero) hero.hidden = true;
      loadStatus.hidden = true;
      currentData = null;
      return;
    }

    showWarnings(messages);
    currentData = result;

    const { fileCounts, dayKeys } = result;
    let dayBit = "";
    if (dayKeys.length > 0) {
      const span = dayKeys.length > 1
        ? `${formatDayKey(dayKeys[0])} → ${formatDayKey(dayKeys[dayKeys.length - 1])}`
        : formatDayKey(dayKeys[0]);
      dayBit = ` · ${dayKeys.length} day${dayKeys.length === 1 ? "" : "s"} (${span}), merged in time order`;
    }
    loadStatus.textContent =
      `Loaded: ${fileCounts.jxv}× JXV, ${fileCounts.jxb}× JXB, ${fileCounts.jxs}× JXS${dayBit}`;
    loadStatus.hidden = false;

    renderAll();
  }

  // Dropped folders: walk the directory tree so a whole DEVICE_ID/ folder (many days) works.
  function entryToFile(entry) {
    return new Promise((resolve) => entry.file(resolve, () => resolve(null)));
  }

  async function readAllEntries(reader) {
    const all = [];
    for (;;) {
      const batch = await new Promise((resolve, reject) => reader.readEntries(resolve, reject));
      if (batch.length === 0) break;
      all.push(...batch);
    }
    return all;
  }

  async function walkEntry(entry, out) {
    if (entry.isFile) {
      const file = await entryToFile(entry);
      if (file) out.push(file);
    } else if (entry.isDirectory) {
      for (const child of await readAllEntries(entry.createReader())) {
        await walkEntry(child, out);
      }
    }
  }

  // The DataTransfer is only readable synchronously inside the drop event, so grab
  // files and entries before the first await.
  async function filesFromDrop(dt) {
    const plainFiles = [...dt.files];
    const entries = [...(dt.items || [])].map((item) =>
      typeof item.webkitGetAsEntry === "function" ? item.webkitGetAsEntry() : null
    );
    if (entries.length === 0 || entries.some((en) => !en)) return plainFiles;
    const out = [];
    for (const entry of entries) await walkEntry(entry, out);
    return out;
  }

  // Drag and drop
  dropZone.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropZone.classList.add("dragover");
  });
  dropZone.addEventListener("dragleave", () => dropZone.classList.remove("dragover"));
  dropZone.addEventListener("drop", async (e) => {
    e.preventDefault();
    dropZone.classList.remove("dragover");
    handleFiles(await filesFromDrop(e.dataTransfer));
  });

  // File picker
  document.getElementById("browse-btn").addEventListener("click", () => fileInput.click());
  fileInput.addEventListener("change", () => {
    handleFiles(fileInput.files);
    fileInput.value = "";
  });

  document.getElementById("load-demo-link").addEventListener("click", () => {
    if (typeof buildJuxtaDemoFiles !== "function") {
      showWarnings(["Demo data loader is unavailable."]);
      return;
    }
    handleFiles(buildJuxtaDemoFiles());
  });

  // Timezone switch re-renders everything
  document.getElementById("tz-select").addEventListener("change", (e) => {
    currentTz = e.target.value;
    renderAll();
  });

  initCopyButton(() => ({ data: currentData, tz: currentTz }));

  // Print: swap Plotly to the light theme (SVG colors are baked in, so CSS
  // print overrides don't reach them) and re-render at print width.
  function rerenderPlotsForTheme(themeName) {
    if (!currentData) return;
    setPlotTheme(themeName);
    renderAllPlots(currentData, currentTz);
    document.querySelectorAll(".plot").forEach((el) => {
      if (el.data) Plotly.Plots.resize(el);
    });
  }
  window.addEventListener("beforeprint", () => rerenderPlotsForTheme("light"));
  window.addEventListener("afterprint", () => rerenderPlotsForTheme("dark"));
})();
