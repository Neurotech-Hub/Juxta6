/* Runs the real pyjuxta library in the browser (Pyodide). Nothing here analyses data:
 * it starts Python, installs the wheel built from /pyjuxta, writes the user's files into
 * Pyodide's virtual disk, and returns what pyjuxta computes. */

const PyRuntime = (() => {
  const PYODIDE_URL = "https://cdn.jsdelivr.net/pyodide/v0.27.7/full/";
  const DATA_DIR = "/data";

  let pyodide = null;
  let manifest = null;
  let queue = Promise.resolve();

  async function start(onStatus) {
    onStatus("Downloading Python…");
    pyodide = await loadPyodide({ indexURL: PYODIDE_URL });
    onStatus("Loading numpy and pandas…");
    await pyodide.loadPackage(["numpy", "pandas", "tzdata"]);
    onStatus("Installing pyJuxta…");
    manifest = await (await fetch("assets/manifest.json", { cache: "no-cache" })).json();
    const wheel = await (await fetch("assets/" + manifest.wheel, { cache: "no-cache" })).arrayBuffer();
    pyodide.unpackArchive(wheel, "wheel");
    pyodide.runPython("import json, shutil, os\nimport pyjuxta as jx");
    return { version: pyodide.runPython("jx.__version__"), manifest };
  }

  // Calls are serialized: Pyodide is single threaded.
  function enqueue(fn) {
    const next = queue.then(fn, fn);
    queue = next.catch(() => {});
    return next;
  }

  function pyError(err) {
    const msg = String((err && err.message) || err);
    const lines = msg.trim().split("\n").filter(Boolean);
    return (lines[lines.length - 1] || msg).replace(/^[A-Za-z]*(Error|Exception): /, "");
  }

  /** Run Python; if `outVar` is given, return JSON.parse(outVar.to_json()). */
  function run(code, outVar) {
    return enqueue(async () => {
      try {
        const tail = outVar ? `\n_pj_out = ${outVar}.to_json(theme="dark")` : "";
        await pyodide.runPythonAsync(code + tail);
        if (!outVar) return null;
        const text = pyodide.globals.get("_pj_out");
        return JSON.parse(text);
      } catch (err) {
        throw new Error(pyError(err));
      }
    });
  }

  /** Evaluate a Python expression that returns a JSON string, and parse it. */
  function evalJSON(expr) {
    return enqueue(async () => {
      try {
        return JSON.parse(await pyodide.runPythonAsync(expr));
      } catch (err) {
        throw new Error(pyError(err));
      }
    });
  }

  function resetData() {
    pyodide.runPython(`shutil.rmtree("${DATA_DIR}", ignore_errors=True)\nos.makedirs("${DATA_DIR}")`);
  }

  function cleanPath(p) {
    return p.split("/").filter((s) => s && s !== "." && s !== "..").join("/");
  }

  function writeFile(relPath, bytes) {
    const full = `${DATA_DIR}/${cleanPath(relPath)}`;
    pyodide.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
    pyodide.FS.writeFile(full, bytes);
  }

  /** Fetch the example deployment (files listed in assets/manifest.json). */
  async function loadExampleFiles(onProgress) {
    const files = manifest.dataset.files;
    resetData();
    let done = 0;
    const pool = 12;
    let next = 0;
    async function worker() {
      while (next < files.length) {
        const rel = files[next++];
        const url = "data/" + rel.split("/").map(encodeURIComponent).join("/");
        const res = await fetch(url);
        if (!res.ok) throw new Error(`Could not fetch ${rel}`);
        writeFile(rel, new Uint8Array(await res.arrayBuffer()));
        onProgress(++done, files.length);
      }
    }
    await Promise.all(Array.from({ length: pool }, worker));
    return files.length;
  }

  /** The files currently on Pyodide's disk as one zip (what the iOS app shares, in one archive). Returns a Uint8Array. */
  function zipData(rootName) {
    return enqueue(async () => {
      const proxy = pyodide.runPython(`
import io, os, re, zipfile
_buf = io.BytesIO()
with zipfile.ZipFile(_buf, "w", zipfile.ZIP_DEFLATED) as _zf:
    for _root, _dirs, _names in sorted(os.walk("${DATA_DIR}")):
        for _n in sorted(_names):
            _p = os.path.join(_root, _n)
            _rel = re.sub(r"^(J[XB]_[0-9A-Fa-f]{6})[^/]*/", r"\\1/", os.path.relpath(_p, "${DATA_DIR}"))
            _zf.write(_p, "${rootName}/" + _rel)
_buf.getvalue()
`);
      try {
        return proxy.toJs();
      } finally {
        proxy.destroy();
      }
    });
  }

  /** `items`: [{ file: File, path: "DEVICE/JXV2026….csv" }] from a picker or a drop. */
  async function loadUserFiles(items, onProgress) {
    resetData();
    let done = 0;
    for (const { file, path } of items) {
      writeFile(path || file.name, new Uint8Array(await file.arrayBuffer()));
      onProgress(++done, items.length);
    }
    return items.length;
  }

  return { start, run, evalJSON, loadExampleFiles, loadUserFiles, zipData, DATA_DIR, get manifest() { return manifest; } };
})();
