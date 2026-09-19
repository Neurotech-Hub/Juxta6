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
    // Unhide before rendering so Plotly can measure the container width
    setHidden("summary-section", false);
    setHidden("plots-section", false);
    setHidden("events-section", false);
    renderSummary(currentData, currentTz);
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

  async function handleFiles(fileList) {
    const files = [...fileList].filter((f) => f.name.toLowerCase().endsWith(".csv"));
    const skipped = fileList.length - files.length;
    if (files.length === 0) {
      showWarnings(["No CSV files found in selection."]);
      return;
    }

    const result = await ingestFiles(files);
    const messages = [...result.warnings];
    if (skipped > 0) messages.push(`Ignored ${skipped} non-CSV file(s).`);

    if (result.error) {
      messages.unshift(result.error);
      showWarnings(messages);
      setHidden("summary-section", true);
      setHidden("plots-section", true);
      setHidden("events-section", true);
      loadStatus.hidden = true;
      currentData = null;
      return;
    }

    showWarnings(messages);
    currentData = result;

    const { fileCounts, deviceId } = result;
    loadStatus.innerHTML =
      `Loaded: ${fileCounts.jxv}× JXV, ${fileCounts.jxb}× JXB, ${fileCounts.jxs}× JXS` +
      (deviceId ? ` &nbsp;·&nbsp; Device: <strong>${deviceId}</strong>` : "");
    loadStatus.hidden = false;

    renderAll();
  }

  // Drag and drop
  dropZone.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropZone.classList.add("dragover");
  });
  dropZone.addEventListener("dragleave", () => dropZone.classList.remove("dragover"));
  dropZone.addEventListener("drop", (e) => {
    e.preventDefault();
    dropZone.classList.remove("dragover");
    handleFiles(e.dataTransfer.files);
  });

  // File picker
  document.getElementById("browse-btn").addEventListener("click", () => fileInput.click());
  fileInput.addEventListener("change", () => {
    handleFiles(fileInput.files);
    fileInput.value = "";
  });

  // Timezone switch re-renders everything
  document.getElementById("tz-select").addEventListener("change", (e) => {
    currentTz = e.target.value;
    renderAll();
  });

  initCopyButton(() => ({ data: currentData, tz: currentTz }));

  // Re-size Plotly charts to match print layout width
  function resizePlots() {
    if (!currentData) return;
    document.querySelectorAll(".plot").forEach((el) => {
      if (el.data) Plotly.Plots.resize(el);
    });
  }
  window.addEventListener("beforeprint", resizePlots);
  window.addEventListener("afterprint", resizePlots);
})();
