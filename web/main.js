// JS glue between the wasm module (embind API, see src/web/AppWeb.cpp) and
// the placeholder panel. The sliders for cell size / speed / wave
// exaggeration run in log10 space to mirror the native UI's log sliders.

/* global createTsunamiModule */

const $ = (id) => document.getElementById(id);

let mod = null;

function heapCall(bytes, fn) {
  const ptr = mod._malloc(bytes.length);
  mod.HEAPU8.set(bytes, ptr);
  try {
    return fn(ptr, bytes.length);
  } finally {
    mod._free(ptr);
  }
}

async function fetchBytes(url, what) {
  $("load-detail").textContent = what;
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  // .gz payloads are stored pre-compressed; browsers inflate natively.
  const body = url.endsWith(".gz")
    ? r.body.pipeThrough(new DecompressionStream("gzip"))
    : r.body;
  return new Uint8Array(await new Response(body).arrayBuffer());
}

function canvasSize() {
  const el = $("view").getBoundingClientRect();
  return [Math.max(1, Math.round(el.width)), Math.max(1, Math.round(el.height))];
}

async function boot() {
  mod = await createTsunamiModule();
  const globe = await fetchBytes("public/data/globe.bin.gz", "Welt-Bathymetrie …");
  $("load-detail").textContent = "Starte Renderer …";
  const [w, h] = canvasSize();
  const ok = heapCall(globe, (p, n) => mod.boot(p, n, w, h, devicePixelRatio));
  if (!ok) {
    $("load-detail").textContent = "Start fehlgeschlagen (Konsole prüfen).";
    return;
  }
  $("loading").classList.add("hidden");

  // Subduction zones load in the background — the app is usable without
  // them; the overlay and real fault geometry appear once parsed.
  fetchBytes("public/data/slab2.bin.gz", "")
    .then((b) => heapCall(b, (p, n) => mod.loadSlab2Bytes(p, n)))
    .catch((e) => console.warn("Slab2 nicht geladen:", e));

  // JS owns the render loop (see AppWeb.cpp renderFrame). rAF pauses in
  // hidden tabs, but renderFrame also polls the displacement worker and the
  // running simulation — the interval fallback keeps that state moving so
  // nothing appears frozen after a tab switch.
  let lastRaf = 0;
  const tick = () => { lastRaf = performance.now(); mod.renderFrame(); requestAnimationFrame(tick); };
  requestAnimationFrame(tick);
  setInterval(() => {
    if (performance.now() - lastRaf > 500) mod.renderFrame();
  }, 250);

  new ResizeObserver(() => {
    const [rw, rh] = canvasSize();
    mod.resize(rw, rh, devicePixelRatio);
  }).observe($("view"));

  buildScenarioButtons();
  wirePanel();
  setInterval(renderPanel, 120);
}

function buildScenarioButtons() {
  const scen = mod.getScenarios();
  const box = $("scenarios");
  for (let i = 0; i < scen.length; i++) {
    const b = document.createElement("button");
    b.textContent = scen[i].name;
    b.onclick = async () => {
      b.disabled = true;
      try {
        const bytes = await fetchBytes(`public/data/scenario_${i}.bin.gz`, "");
        heapCall(bytes, (p, n) => mod.loadScenarioBytes(i, p, n));
      } finally {
        b.disabled = false;
      }
    };
    box.appendChild(b);
  }
}

// Slider helpers (log10 mapping)
const log = {
  toUi: (v) => Math.log10(Math.max(1, v)),
  fromUi: (u) => Math.pow(10, parseFloat(u)),
};

function wirePanel() {
  $("btn-load").onclick = () => mod.loadSelection();
  $("btn-clear-sel").onclick = () => mod.clearSelection();
  $("btn-back").onclick = () => mod.backToGlobe();

  $("mw").oninput = (e) => mod.setMw(parseFloat(e.target.value));
  $("mw").onchange = () => mod.commitMw();

  $("btn-field-bathy").onclick = () => mod.setField(0);
  $("btn-field-disp").onclick = () => mod.setField(1);
  $("vex").oninput = (e) => mod.setVertExaggeration(parseFloat(e.target.value));
  $("wex").oninput = (e) => mod.setWaveExaggeration(log.fromUi(e.target.value));
  $("show-sea").onchange = (e) => mod.setShowSea(e.target.checked);

  $("cell").oninput = (e) => mod.setSimCellSize(log.fromUi(e.target.value));
  $("speed").oninput = (e) => mod.setSimSpeed(log.fromUi(e.target.value));
  $("auto-speed").onchange = (e) => mod.setSimAutoSpeed(e.target.checked);

  $("btn-sim").onclick = () => {
    const s = mod.getState();
    if (s.sim.running) mod.stopSim();
    else mod.startSim();
  };
}

const fmt = (v, d = 1) => Number(v).toFixed(d);

function renderPanel() {
  const s = mod.getState();
  const globe = s.state === "globe";
  $("p-globe").classList.toggle("hidden", !globe);
  $("p-region").classList.toggle("hidden", globe);
  $("error").textContent = s.error || "";

  if (globe) {
    const has = s.selection.has;
    $("btn-load").classList.toggle("hidden", !has);
    $("btn-clear-sel").classList.toggle("hidden", !has);
    $("sel-info").textContent = has
      ? `Lon ${fmt(s.selection.lonMin)}° … ${fmt(s.selection.lonMax)}°,  ` +
        `Lat ${fmt(s.selection.latMin)}° … ${fmt(s.selection.latMax)}°`
      : "Bereich auf der Karte ziehen.";
    return;
  }

  const r = s.region;
  $("reg-info").textContent =
    `Lon ${fmt(r.lonMin, 2)}° – ${fmt(r.lonMax, 2)}°  ·  ` +
    `Lat ${fmt(r.latMin, 2)}° – ${fmt(r.latMax, 2)}°  ·  ` +
    `Gitter ${r.gridW} × ${r.gridH}`;

  // Quake
  const q = s.quake;
  if (document.activeElement !== $("mw")) $("mw").value = q.mw;
  $("mw-val").textContent = fmt(q.mw);
  $("quake-info").textContent = q.computing
    ? "Displacement wird berechnet …"
    : q.hasClick
      ? `Epizentrum ${fmt(q.epiLon, 2)}°, ${fmt(q.epiLat, 2)}°  ·  ` +
        `Versatz ${fmt(q.slip, 2)} m · L ${fmt(q.length / 1000, 0)} km · ` +
        `B ${fmt(q.width / 1000, 0)} km (Fallback-Geometrie)`
      : "(noch kein Epizentrum — Klick aufs Terrain)";

  // Display
  $("btn-field-bathy").classList.toggle("primary", r.field === 0);
  $("btn-field-disp").classList.toggle("primary", r.field === 1);
  if (document.activeElement !== $("vex")) $("vex").value = r.vertExag;
  $("vex-val").textContent = `${fmt(r.vertExag, 0)}×`;
  if (document.activeElement !== $("wex")) $("wex").value = log.toUi(r.waveExag);
  $("wex-val").textContent = `${fmt(r.waveExag, 0)}×`;
  $("show-sea").checked = r.showSea;

  // Simulation
  const sim = s.sim;
  if (document.activeElement !== $("cell")) $("cell").value = log.toUi(sim.cellSize);
  $("cell-val").textContent = `${fmt(sim.cellSize, 0)} m`;
  if (document.activeElement !== $("speed")) $("speed").value = log.toUi(sim.speed);
  $("speed-val").textContent = `${fmt(sim.speed, 0)}×`;
  $("auto-speed").checked = sim.autoSpeed;
  if (sim.previewNx)
    $("grid-preview").textContent =
      `→ ${sim.previewNx} × ${sim.previewNy} Zellen` +
      (sim.effCellSize > sim.cellSize * 1.01
        ? ` (begrenzt: eff. ${fmt(sim.effCellSize, 0)} m)`
        : "");

  const btn = $("btn-sim");
  if (sim.running) {
    btn.textContent = "Stop";
    btn.classList.remove("primary");
    btn.classList.add("danger");
    const t = sim.time;
    const hh = Math.floor(t / 3600), mm = Math.floor(t / 60) % 60,
          ss = Math.floor(t) % 60;
    $("sim-info").textContent =
      `läuft – ${sim.steps} Schritte · seit Erdbeben ` +
      (hh > 0 ? `${hh} h ${mm} min` : mm > 0 ? `${mm} min ${ss} s` : `${fmt(t)} s`) +
      (sim.maxSpeed > 0 && sim.autoSpeed ? ` · ~${fmt(sim.maxSpeed, 0)}×` : "");
  } else {
    btn.textContent = "Simulieren »";
    btn.classList.add("primary");
    btn.classList.remove("danger");
    btn.disabled = q.computing;
    $("sim-info").textContent = "";
  }
}

boot().catch((e) => {
  console.error(e);
  $("load-detail").textContent = String(e);
});
