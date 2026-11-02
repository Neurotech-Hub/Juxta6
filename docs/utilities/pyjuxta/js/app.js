/* Walkthrough UI: load a deployment, build each step's settings, run pyjuxta, draw what it returns. */

(() => {
  const $ = (sel, root = document) => root.querySelector(sel);
  const el = (tag, attrs = {}, html) => {
    const n = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === "class") n.className = v;
      else if (k.startsWith("data-")) n.setAttribute(k, v);
      else n[k] = v;
    }
    if (html !== undefined) n.innerHTML = html;
    return n;
  };

  const EXAMPLE = {
    tz: "America/Chicago",
    labels: { JX_54F7A1: "Human", JX_5C0848: "Dog", JB_83FA0F: "Garage", JB_6A9D3A: "Bedroom", JB_24563F: "Work desk", JB_0A3660: "Kitchen" },
  };
  const TIMEZONES = [
    "UTC", "America/New_York", "America/Chicago", "America/Denver", "America/Phoenix", "America/Los_Angeles", "America/Anchorage",
    "Pacific/Honolulu", "America/Toronto", "America/Mexico_City", "America/Sao_Paulo", "Europe/London", "Europe/Dublin",
    "Europe/Paris", "Europe/Berlin", "Europe/Madrid", "Europe/Rome", "Europe/Athens", "Africa/Johannesburg", "Asia/Dubai",
    "Asia/Kolkata", "Asia/Bangkok", "Asia/Shanghai", "Asia/Tokyo", "Asia/Seoul", "Australia/Sydney", "Pacific/Auckland",
  ];

  const S = {
    ready: false,
    isExample: false,
    tz: "UTC",
    devices: [], // [{id, label, role, vitals, peers}]
    ids: [],
    mobiles: [],
    bases: [],
    byId: {},
    name: (id) => (S.byId[id] && S.byId[id].label) || id,
    ref: (id) => {
      const d = S.byId[id];
      if (!d || !d.label) return id;
      const clash = S.devices.some((o) => o.id !== id && (o.id.toLowerCase() === d.label.toLowerCase() || (o.label || "").toLowerCase() === d.label.toLowerCase()));
      return clash ? id : d.label;
    },
  };

  /* -------------------------------------------------------------------- status */
  const statusEl = $("#py-status");
  function setStatus(text, state = "busy") {
    statusEl.textContent = text;
    statusEl.dataset.state = state;
  }
  const loadMsg = $("#load-msg");
  function setLoadMsg(text, kind = "") {
    loadMsg.textContent = text;
    loadMsg.dataset.kind = kind;
    loadMsg.hidden = !text;
  }

  /* ------------------------------------------------------------------ sections */
  const secs = SECTIONS.map((def, i) => ({ def, n: i + 1, p: {}, dirty: true, visible: false, token: 0, timer: null, root: null, outs: [] }));

  function defaultsFor(sec) {
    const p = {};
    for (const c of sec.def.controls) {
      if (c.type === "devices") p[c.id] = (c.def ? c.def(S) : S.ids).filter((id) => S.byId[id]);
      else if (c.type === "device") p[c.id] = c.def ? c.def(S) : S.ids[0];
      else p[c.id] = c.value;
    }
    return p;
  }

  function reconcile(sec) {
    const fresh = defaultsFor(sec);
    for (const c of sec.def.controls) {
      if (c.type === "devices") {
        const pool = poolOf(c.pool);
        sec.p[c.id] = (sec.p[c.id] || []).filter((id) => pool.includes(id));
        if (sec.p[c.id].length === 0 && !sec.touched?.[c.id]) sec.p[c.id] = fresh[c.id];
      } else if (c.type === "device") {
        if (!poolOf(c.pool).includes(sec.p[c.id])) sec.p[c.id] = fresh[c.id];
      } else if (sec.p[c.id] === undefined) sec.p[c.id] = fresh[c.id];
    }
  }

  function buildSteps() {
    const host = $("#steps");
    const nav = $("#sidenav-steps");
    for (const sec of secs) {
      const { def } = sec;
      const root = el("section", { class: "step card", id: `step-${def.id}` });
      root.innerHTML = `
        <header class="step-head"><span class="step-n">${sec.n}</span><h2>${def.title}</h2></header>
        <p class="question">${def.question}</p>
        <div class="settings"><div class="settings-main"></div><details class="adv" hidden><summary>More settings</summary><div class="settings-adv"></div></details></div>
        <p class="try"><b>Try this.</b> ${def.tryThis}</p>
        <div class="placeholder" role="status"></div>
        <div class="outs"></div>`;
      host.appendChild(root);
      sec.root = root;
      sec.ph = $(".placeholder", root);
      sec.outsEl = $(".outs", root);
      for (const out of def.outputs) {
        const box = el("div", { class: "out" });
        box.innerHTML = `
          ${out.heading ? `<h3 class="out-title">${out.heading}</h3>` : ""}
          <div class="plot"></div>
          <div class="quality"></div>
          <div class="readout"></div>
          <div class="tables"></div>
          <ul class="notes"></ul>
          <details class="code"><summary>Python behind this figure</summary><div class="code-body"><pre><code></code></pre><button class="btn small copy" type="button">Copy</button></div></details>`;
        sec.outsEl.appendChild(box);
        sec.outs.push({ def: out, box, plot: $(".plot", box), code: $("code", box) });
        $(".copy", box).addEventListener("click", (e) => copyText($("code", box).textContent, e.currentTarget));
      }
      const li = el("li", {}, `<a href="#step-${def.id}" data-step="${def.id}"><span>${sec.n}</span>${def.title.replace(/\?$/, "")}</a>`);
      nav.appendChild(li);
    }
  }

  function buildControls(sec) {
    const main = $(".settings-main", sec.root);
    const adv = $(".settings-adv", sec.root);
    main.innerHTML = "";
    adv.innerHTML = "";
    sec.touched = sec.touched || {};
    for (const c of sec.def.controls) {
      const wrap = el("div", { class: "ctl" + (c.type === "devices" ? " wide" : "") });
      const help = c.help ? `<small>${c.help}</small>` : "";
      if (c.type === "range") {
        const id = `${sec.def.id}-${c.id}`;
        wrap.innerHTML = `<label for="${id}"><span>${c.label}</span><output></output></label><input id="${id}" type="range" min="${c.min}" max="${c.max}" step="${c.step}">${help}`;
        const input = $("input", wrap), out = $("output", wrap);
        input.value = sec.p[c.id];
        out.textContent = c.fmt(Number(input.value));
        input.addEventListener("input", () => (out.textContent = c.fmt(Number(input.value))));
        input.addEventListener("change", () => changed(sec, c, Number(input.value)));
      } else if (c.type === "number") {
        const id = `${sec.def.id}-${c.id}`;
        wrap.innerHTML = `<label for="${id}"><span>${c.label}</span></label><input id="${id}" type="number" min="${c.min}" max="${c.max}" step="1">${help}`;
        const input = $("input", wrap);
        input.value = sec.p[c.id];
        input.addEventListener("change", () => changed(sec, c, Math.max(c.min, Math.min(c.max, Math.round(Number(input.value) || 0)))));
      } else if (c.type === "select") {
        const id = `${sec.def.id}-${c.id}`;
        wrap.innerHTML = `<label for="${id}"><span>${c.label}</span></label><select id="${id}"></select>${help}`;
        const sel = $("select", wrap);
        for (const [v, text] of c.options) sel.appendChild(el("option", { value: String(v), textContent: text, selected: String(v) === String(sec.p[c.id]) }));
        const numeric = typeof c.options[0][0] === "number";
        sel.addEventListener("change", () => changed(sec, c, numeric ? Number(sel.value) : sel.value));
      } else if (c.type === "device") {
        const id = `${sec.def.id}-${c.id}`;
        const pool = poolOf(c.pool);
        wrap.innerHTML = `<label for="${id}"><span>${c.label}</span></label><select id="${id}"></select>${help}`;
        const sel = $("select", wrap);
        for (const d of pool) sel.appendChild(el("option", { value: d, textContent: S.name(d), selected: d === sec.p[c.id] }));
        sel.addEventListener("change", () => changed(sec, c, sel.value));
      } else if (c.type === "devices") {
        wrap.innerHTML = `<label><span>${c.label}</span></label><div class="chips"></div>${help}`;
        const chips = $(".chips", wrap);
        for (const d of poolOf(c.pool)) {
          const chip = el("label", { class: "chip" });
          chip.innerHTML = `<input type="checkbox" value="${U.esc(d)}"><span>${U.esc(S.name(d))}</span>`;
          const cb = $("input", chip);
          cb.checked = sec.p[c.id].includes(d);
          cb.addEventListener("change", () => {
            const picked = $$all(chips).filter((x) => x.checked).map((x) => x.value);
            sec.touched[c.id] = true;
            changed(sec, c, picked);
          });
          chips.appendChild(chip);
        }
      }
      (c.adv ? adv : main).appendChild(wrap);
    }
    $("details.adv", sec.root).hidden = adv.children.length === 0;
  }
  const $$all = (chips) => Array.from(chips.querySelectorAll("input"));
  function poolOf(pool) {
    return pool === "mobiles" ? S.mobiles : pool === "bases" ? S.bases : S.ids;
  }

  function changed(sec, c, value) {
    sec.p[c.id] = value;
    schedule(sec, 120);
  }

  /* ------------------------------------------------------------------- running */
  function schedule(sec, delay = 0) {
    sec.dirty = true;
    clearTimeout(sec.timer);
    sec.timer = setTimeout(() => maybeRender(sec), delay);
  }

  function maybeRender(sec) {
    if (!S.ready || !sec.dirty || !sec.visible) return;
    renderSection(sec);
  }

  function showPlaceholder(sec, text, kind = "") {
    sec.ph.hidden = !text;
    sec.ph.textContent = text || "";
    sec.ph.dataset.kind = kind;
    sec.outsEl.hidden = !!text;
  }

  function toRecords(t) {
    return t.rows.map((row) => Object.fromEntries(t.columns.map((c, i) => [c, row[i]])));
  }

  async function renderSection(sec) {
    const token = ++sec.token;
    sec.dirty = false;
    reconcile(sec);
    const msg = sec.def.needs && sec.def.needs(S);
    if (msg) return showPlaceholder(sec, msg, "info");
    for (const c of sec.def.controls) {
      if (c.type === "devices" && sec.p[c.id].length === 0) return showPlaceholder(sec, "Pick at least one device above.", "info");
    }
    showPlaceholder(sec, "");
    sec.root.classList.add("busy");
    for (const out of sec.outs) {
      let code;
      try {
        code = out.def.code(sec.p, S);
        out.code.textContent = code + "\n# res.show() draws this figure in a notebook; res.summary and res.tables hold the numbers.";
        const res = await PyRuntime.run(code, "res");
        if (token !== sec.token) return;
        res.rec = Object.fromEntries(Object.entries(res.tables).map(([k, t]) => [k, toRecords(t)]));
        drawOutput(sec, out, res);
        out.error = null;
      } catch (err) {
        if (token !== sec.token) return;
        out.plot.innerHTML = "";
        $(".readout", out.box).innerHTML = `<p class="error"><b>pyJuxta stopped:</b> ${U.esc(err.message)}</p>`;
        $(".tables", out.box).innerHTML = "";
        $(".quality", out.box).textContent = "";
        $(".notes", out.box).innerHTML = "";
        out.error = err.message;
      }
    }
    if (token === sec.token) sec.root.classList.remove("busy");
  }

  function fmtCell(v, f) {
    if (v === null || v === undefined) return "–";
    if (f === "pct") return U.pct(v, 0);
    if (typeof f === "number") return Number(v).toFixed(f);
    if (typeof v === "number" && !Number.isInteger(v)) return v.toFixed(1);
    return U.esc(v);
  }

  function drawOutput(sec, out, res) {
    const fig = res.figure;
    if (fig) {
      const layout = Object.assign({}, fig.layout, { autosize: true });
      Plotly.react(out.plot, fig.data, layout, { responsive: true, displaylogo: false, displayModeBar: "hover" });
    }
    const q = res.quality || {};
    const bits = [];
    if (U.has(q.coverage)) bits.push(`${U.pct(q.coverage)} of the recorded time covered`);
    if (U.has(q.n_observations)) bits.push(`${q.n_observations.toLocaleString()} observations`);
    $(".quality", out.box).textContent = bits.join(" · ");
    $(".readout", out.box).innerHTML = out.def.read ? out.def.read(res, sec.p, S) : "";
    const tablesEl = $(".tables", out.box);
    tablesEl.innerHTML = "";
    for (const spec of out.def.tables || []) {
      const rows = res.rec[spec.key];
      if (!rows || !rows.length) continue;
      const head = spec.cols.map(([, label]) => `<th>${label}</th>`).join("");
      const body = rows.map((r) => `<tr>${spec.cols.map(([f, , fmt]) => `<td>${fmtCell(r[f], fmt)}</td>`).join("")}</tr>`).join("");
      tablesEl.insertAdjacentHTML("beforeend", `${spec.title ? `<h4>${spec.title}</h4>` : ""}<div class="tbl-wrap"><table class="data-table"><thead><tr>${head}</tr></thead><tbody>${body}</tbody></table></div>`);
    }
    $(".notes", out.box).innerHTML = (res.notes || []).map((n) => `<li>${U.esc(n)}</li>`).join("");
  }

  /* ------------------------------------------------------------------ the data */
  async function applyDeployment(opts) {
    // `dep` is already loaded in Python; read it back.
    const info = await PyRuntime.evalJSON(`json.dumps({
      "devices": [{"id": i, "label": dep.label(i) if dep.label(i) != i else "", "role": dep.role(i),
                   "vitals": int(len(d.vitals)), "peers": int(len(d.peers))} for i, d in dep.devices.items()],
      "warnings": dep.check(), "span": [int(x) for x in dep.span] if dep.span else None, "info": dep.info})`);
    S.devices = info.devices;
    S.byId = Object.fromEntries(S.devices.map((d) => [d.id, d]));
    refreshRoles();
    S.span = info.span;
    S.warnings = info.warnings;
    S.fileInfo = info.info;
    S.ready = true;
    renderLoaded();
    for (const sec of secs) {
      sec.p = defaultsFor(sec);
      sec.touched = {};
      buildControls(sec);
      sec.dirty = true;
      sec.root.hidden = false;
      showPlaceholder(sec, "Waiting to run…", "info");
    }
    kick();
  }

  function refreshRoles() {
    // Mobiles first, so the tags that move lead every list.
    S.devices.sort((a, b) => (a.role === b.role ? a.id.localeCompare(b.id) : a.role === "mobile" ? -1 : 1));
    S.ids = S.devices.map((d) => d.id);
    S.mobiles = S.devices.filter((d) => d.role === "mobile").map((d) => d.id);
    S.bases = S.devices.filter((d) => d.role === "base").map((d) => d.id);
  }

  function kick() {
    for (const sec of secs) {
      if (sec.dirty && sec.visible) schedule(sec, 0);
    }
  }

  function defaultRole(id) {
    return /^JB_/.test(id) ? "base" : "mobile";
  }

  function fmtDate(unix) {
    return new Date(unix * 1000).toISOString().slice(0, 16).replace("T", " ") + " UTC";
  }

  function renderLoaded() {
    $("#loaded").hidden = false;
    const nMobile = S.mobiles.length, nBase = S.bases.length;
    const files = S.fileInfo && S.fileInfo.files_read;
    $("#load-summary").innerHTML = `<b>${S.isExample ? "Example deployment" : "Your data"}:</b> ${U.pl(S.devices.length, "tag")} (${nMobile} mobile, ${nBase} base)${files ? ` from ${U.pl(files, "file")}` : ""}${S.span ? `, ${fmtDate(S.span[0])} to ${fmtDate(S.span[1])}` : ""}.`;
    const body = $("#dev-body");
    body.innerHTML = "";
    for (const d of S.devices) {
      const tr = el("tr");
      tr.innerHTML = `<td><code>${U.esc(d.id)}</code></td>
        <td><input type="text" class="name-in" value="${U.esc(d.label || "")}" placeholder="${U.esc(d.id)}" maxlength="24" aria-label="Name for ${U.esc(d.id)}"></td>
        <td><select class="role-in" aria-label="Role for ${U.esc(d.id)}"><option value="mobile">mobile (worn)</option><option value="base">base (fixed)</option></select></td>
        <td class="num">${d.vitals.toLocaleString()}</td><td class="num">${d.peers.toLocaleString()}</td>`;
      const role = $(".role-in", tr);
      role.value = d.role;
      $(".name-in", tr).addEventListener("change", (e) => onLabel(d, e.target));
      role.addEventListener("change", () => onRole(d, role.value));
      body.appendChild(tr);
    }
    const warn = $("#load-warnings");
    warn.innerHTML = (S.warnings || []).map((w) => `<li>${U.esc(w)}</li>`).join("");
    warn.hidden = !(S.warnings || []).length;
    if (!$("#tz").dataset.built) buildTz();
    $("#tz").value = S.tz;
    updateSetupCode();
  }

  async function onLabel(d, input) {
    const value = input.value.trim();
    const lower = value.toLowerCase();
    const clash = value && S.devices.some((o) => o.id !== d.id && (o.id.toLowerCase() === lower || (o.label || "").toLowerCase() === lower));
    if (clash) {
      input.value = d.label || "";
      setLoadMsg(`"${value}" is already used by another tag. Names must be different.`, "warn");
      return;
    }
    setLoadMsg("");
    try {
      await PyRuntime.run(`dep.set_labels({${U.str(d.id)}: ${U.str(value)}})`);
    } catch (err) {
      input.value = d.label || "";
      return setLoadMsg(err.message, "error");
    }
    d.label = value;
    refreshAfterEdit();
  }

  async function onRole(d, role) {
    try {
      await PyRuntime.run(`dep.set_roles({${U.str(d.id)}: ${U.str(role)}})`);
    } catch (err) {
      return setLoadMsg(err.message, "error");
    }
    d.role = role;
    refreshRoles();
    refreshAfterEdit();
  }

  function refreshAfterEdit() {
    updateSetupCode();
    for (const sec of secs) {
      reconcile(sec);
      buildControls(sec);
      sec.dirty = true;
    }
    kick();
  }

  function updateSetupCode() {
    const labels = S.devices.filter((d) => d.label).map((d) => `${U.str(d.id)}: ${U.str(d.label)}`);
    const roles = S.devices.filter((d) => d.role !== defaultRole(d.id)).map((d) => `${U.str(d.id)}: ${U.str(d.role)}`);
    const src = S.isExample ? "data/" : "path/to/your/folder-or-zip";
    let code = `import pyjuxta as jx\n\ndep = jx.load_deployment(${U.str(src)}, timezone=${U.str(S.tz)})`;
    if (labels.length) code += `\ndep.set_labels({${labels.join(", ")}})`;
    if (roles.length) code += `\ndep.set_roles({${roles.join(", ")}})`;
    code += `\ndep.check()   # plain-language data-quality findings`;
    $("#setup-code").textContent = code;
  }

  function buildTz() {
    const sel = $("#tz");
    const zones = new Set(TIMEZONES);
    let browser = "UTC";
    try { browser = Intl.DateTimeFormat().resolvedOptions().timeZone || "UTC"; } catch (e) { /* default */ }
    zones.add(browser);
    for (const z of [...zones].sort()) sel.appendChild(el("option", { value: z, textContent: z.replace(/_/g, " ") }));
    sel.dataset.built = "1";
    S.browserTz = browser;
    sel.addEventListener("change", async () => {
      try {
        await PyRuntime.run(`dep.set_timezone(${U.str(sel.value)})`);
      } catch (err) {
        sel.value = S.tz;
        return setLoadMsg(err.message, "error");
      }
      S.tz = sel.value;
      updateSetupCode();
      for (const sec of secs) sec.dirty = true;
      kick();
    });
  }

  async function loadWith(label, loader, isExample) {
    const hadData = S.devices.length > 0;
    S.ready = false;
    $("#btn-example").disabled = true;
    setStatus("Loading data…");
    setLoadMsg("Loading…", "busy");
    try {
      const count = await loader((done, total) => setLoadMsg(`Reading files ${done} of ${total}…`, "busy"));
      if (!count) throw new Error("No CSV or zip files found. Choose the folder that holds the DEVICE_ID folders or zips.");
      const tz = isExample ? EXAMPLE.tz : S.browserTz || "UTC";
      setLoadMsg("Running pyjuxta.load_deployment…", "busy");
      // Load into a new object first so a failed load never replaces the working deployment.
      await PyRuntime.run(
        `_new = jx.load_deployment("${PyRuntime.DATA_DIR}", timezone=${U.str(tz)})\n` +
          `if not _new.devices:\n    raise ValueError("No pyJuxta data found in those files. Expect DEVICE_ID folders or zips holding JXV, JXB and JXS CSV files.")\n` +
          `dep = _new`
      );
      S.isExample = isExample;
      S.tz = tz;
      if (isExample) await PyRuntime.run(`dep.set_labels(${JSON.stringify(EXAMPLE.labels)})`);
      await applyDeployment();
      setLoadMsg("");
    } catch (err) {
      S.ready = hadData;
      setLoadMsg(err.message, "error");
      if (!hadData) secs.forEach((s) => showPlaceholder(s, "Load a deployment above to run this step.", "info"));
    } finally {
      setStatus(S.version ? `pyJuxta ${S.version} ready` : "Python failed to start", S.version ? "ready" : "error");
      $("#btn-example").disabled = false;
    }
  }

  const loadExample = () => loadWith("example", (p) => PyRuntime.loadExampleFiles(p), true);
  const loadUser = (items) => {
    const keep = items.filter((i) => /\.(csv|zip)$/i.test(i.path || i.file.name));
    return loadWith("user", (p) => PyRuntime.loadUserFiles(keep, p), false);
  };

  /* ------------------------------------------------------------- file picking */
  async function itemsFromDrop(dt) {
    const entries = [...(dt.items || [])].map((i) => (i.webkitGetAsEntry ? i.webkitGetAsEntry() : null)).filter(Boolean);
    if (!entries.length) return [...dt.files].map((file) => ({ file, path: file.name }));
    const out = [];
    const readBatch = (reader) => new Promise((res, rej) => reader.readEntries(res, rej));
    async function walk(entry, prefix) {
      if (entry.isFile) {
        const file = await new Promise((res, rej) => entry.file(res, rej));
        out.push({ file, path: prefix + entry.name });
      } else if (entry.isDirectory) {
        const reader = entry.createReader();
        let batch;
        do {
          batch = await readBatch(reader);
          for (const e of batch) await walk(e, prefix + entry.name + "/");
        } while (batch.length);
      }
    }
    for (const e of entries) await walk(e, "");
    return out;
  }

  function wirePickers() {
    $("#btn-example").addEventListener("click", loadExample);
    $("#btn-folder").addEventListener("click", () => $("#file-folder").click());
    $("#btn-files").addEventListener("click", () => $("#file-files").click());
    $("#file-folder").addEventListener("change", (e) => {
      const items = [...e.target.files].map((file) => ({ file, path: file.webkitRelativePath || file.name }));
      e.target.value = "";
      if (items.length) loadUser(items);
    });
    $("#file-files").addEventListener("change", (e) => {
      const items = [...e.target.files].map((file) => ({ file, path: file.name }));
      e.target.value = "";
      if (items.length) loadUser(items);
    });
    const drop = $("#drop");
    ["dragenter", "dragover"].forEach((ev) => drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.add("over"); }));
    ["dragleave", "drop"].forEach((ev) => drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.remove("over"); }));
    drop.addEventListener("drop", async (e) => {
      const items = await itemsFromDrop(e.dataTransfer);
      if (items.length) loadUser(items);
    });
    // Do not let a miss open the file in the tab.
    window.addEventListener("dragover", (e) => e.preventDefault());
    window.addEventListener("drop", (e) => e.preventDefault());
  }

  /* ------------------------------------------------------------------- misc UI */
  async function copyText(text, btn) {
    try {
      await navigator.clipboard.writeText(text);
      const old = btn.textContent;
      btn.textContent = "Copied";
      setTimeout(() => (btn.textContent = old), 1200);
    } catch (e) {
      btn.textContent = "Select and copy";
    }
  }

  function fullScript() {
    const parts = [$("#setup-code").textContent];
    for (const sec of secs) {
      if (sec.def.needs && sec.def.needs(S)) continue;
      parts.push(`\n# ${sec.n}. ${sec.def.title}`);
      for (const out of sec.outs) {
        try { parts.push(out.def.code(sec.p, S)); } catch (e) { /* skip */ }
      }
    }
    return parts.join("\n") + "\n";
  }

  function wireCopy() {
    $("#btn-copy-setup").addEventListener("click", (e) => copyText($("#setup-code").textContent, e.currentTarget));
    $("#btn-copy-all").addEventListener("click", (e) => {
      if (!S.ready) return;
      copyText(fullScript(), e.currentTarget);
    });
  }

  function observe() {
    const io = new IntersectionObserver(
      (entries) => {
        for (const e of entries) {
          const sec = secs.find((s) => s.root === e.target);
          sec.visible = e.isIntersecting;
          if (e.isIntersecting) maybeRender(sec);
        }
      },
      { rootMargin: "400px 0px 400px 0px" }
    );
    secs.forEach((s) => io.observe(s.root));

    const links = [...document.querySelectorAll("#sidenav-steps a, #sidenav-data a")];
    const spy = () => {
      let current = null;
      for (const l of links) {
        const t = document.getElementById(l.getAttribute("href").slice(1));
        if (t && t.getBoundingClientRect().top < 160) current = l;
      }
      links.forEach((l) => l.classList.toggle("active", l === current));
      // Backstop for IntersectionObserver, which browsers may defer for hidden tabs.
      for (const sec of secs) {
        const r = sec.root.getBoundingClientRect();
        const near = r.bottom > -400 && r.top < window.innerHeight + 400;
        if (near && !sec.visible) {
          sec.visible = true;
          maybeRender(sec);
        } else if (!near) sec.visible = false;
      }
    };
    window.addEventListener("scroll", spy, { passive: true });
    spy();
  }

  /* --------------------------------------------------------------------- boot */
  async function boot() {
    buildSteps();
    wirePickers();
    wireCopy();
    buildTz();
    observe();
    secs.forEach((s) => showPlaceholder(s, "Starting Python and loading the example data…", "info"));
    $("#btn-example").disabled = true;
    try {
      const info = await PyRuntime.start((t) => setStatus(t));
      S.version = info.version;
      setStatus(`pyJuxta ${info.version} ready`, "ready");
    } catch (err) {
      setStatus("Python failed to start", "error");
      const msg = `Could not start Python in this browser (${err.message}). The walkthrough needs to download Pyodide from a CDN; check your connection and reload.`;
      setLoadMsg(msg, "error");
      secs.forEach((s) => showPlaceholder(s, msg, "error"));
      return;
    }
    $("#btn-example").disabled = false;
    await loadExample();
  }

  window.addEventListener("DOMContentLoaded", boot);
})();
