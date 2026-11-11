/* Demo package: one real day of the Dog tag from the pyJuxta example deployment.
 * Files live under ../pyjuxta/data/JX_5C0848/ (same tree as GitHub Pages and
 * `python3 -m http.server` from docs/). Serve over HTTP so fetch can reach them.
 */

(function (global) {
  /** Full production day for the leash tag (see pyjuxta/data/README.md). */
  const DEMO_DATE = "20261031";
  const DEMO_DEVICE = "JX_5C0848";
  const DEMO_BASE = `../pyjuxta/data/${DEMO_DEVICE}`;

  async function fetchCsv(name) {
    const url = `${DEMO_BASE}/${name}`;
    const res = await fetch(url, { cache: "no-cache" });
    if (!res.ok) {
      throw new Error(
        `Could not load ${name} (${res.status}). Serve docs/ over HTTP so the plotter can read ${DEMO_BASE}/.`
      );
    }
    const text = await res.text();
    return new File([text], name, { type: "text/csv" });
  }

  /**
   * Returns JXS / JXV / JXB for DEMO_DATE as File objects the drop handler already accepts.
   * @returns {Promise<File[]>}
   */
  global.buildJuxtaDemoFiles = async function buildJuxtaDemoFiles() {
    const names = [`JXS${DEMO_DATE}.csv`, `JXV${DEMO_DATE}.csv`, `JXB${DEMO_DATE}.csv`];
    return Promise.all(names.map(fetchCsv));
  };

  global.juxtaDemoMeta = { date: DEMO_DATE, device: DEMO_DEVICE, label: "Dog · 31 Oct 2026" };
})(typeof window !== "undefined" ? window : globalThis);
