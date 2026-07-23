// Bridge to the embind API exported by src/web/AppWeb.cpp.

export interface SlabInfo {
  valid: boolean;
  depth?: number;
  strike?: number;
  dip?: number;
  region?: string;
}

export interface HoverInfo {
  valid: boolean;
  lon?: number;
  lat?: number;
  slab?: SlabInfo;
}

export interface Scenario {
  name: string;
  mw: number;
  epiLon: number;
  epiLat: number;
}

export interface StationSample {
  t: number; // simulated seconds since the quake
  eta: number; // sea-surface anomaly (h + b), metres
}

export interface Station {
  name: string;
  lon: number;
  lat: number;
  series: StationSample[];
}

export interface AppSnapshot {
  state: "globe" | "region";
  error: string;
  placingStation: boolean;
  selection: {
    has: boolean;
    lonMin?: number;
    lonMax?: number;
    latMin?: number;
    latMax?: number;
  };
  region?: {
    loaded: boolean;
    lonMin: number;
    lonMax: number;
    latMin: number;
    latMax: number;
    gridW: number;
    gridH: number;
    field: 0 | 1;
    showSea: boolean;
    vertExag: number;
    waveExag: number;
    waterAnom: number;
  };
  quake: {
    hasClick: boolean;
    mw: number;
    epiLon: number;
    epiLat: number;
    computing: boolean;
    hasDisplacement: boolean;
    slip: number;
    length: number;
    width: number;
    ifaceScaling: boolean;
    inCoverage: boolean;
    slabDepth?: number;
    slabStrike?: number;
    slabDip?: number;
    slabRegion?: string;
  };
  slab: { available: boolean; restrict: boolean; showOverlay: boolean };
  sim: {
    running: boolean;
    cellSize: number;
    speed: number;
    autoSpeed: boolean;
    time?: number;
    steps?: number;
    maxSpeed?: number;
    previewNx?: number;
    previewNy?: number;
    effCellSize?: number;
  };
}

export interface TsunamiModule {
  _malloc(n: number): number;
  _free(p: number): void;
  HEAPU8: Uint8Array;
  boot(
    ptr: number,
    size: number,
    cssW: number,
    cssH: number,
    dpr: number,
  ): boolean;
  renderFrame(): void;
  resize(cssW: number, cssH: number, dpr: number): void;
  getState(): AppSnapshot;
  getScenarios(): Scenario[];
  getHoverInfo(x: number, y: number): HoverInfo;
  loadScenarioBytes(idx: number, ptr: number, size: number): boolean;
  loadSlab2Bytes(ptr: number, size: number): boolean;
  loadSelection(): boolean;
  loadSelectionTiles(tiles: { ptr: number; size: number }[]): boolean;
  backToGlobe(): void;
  reloadRegion(maxDim: number): void;
  setSelection(
    lonMin: number,
    lonMax: number,
    latMin: number,
    latMax: number,
  ): void;
  clearSelection(): void;
  setMaxSelDeg(v: number): void;
  setMw(v: number): void;
  commitMw(): void;
  clearQuake(): void;
  setPlacingStation(v: boolean): void;
  addStation(lon: number, lat: number): number;
  renameStation(idx: number, name: string): void;
  removeStation(idx: number): void;
  clearStations(): void;
  getStations(): Station[];
  setStationMarkers(
    markers: { lon: number; lat: number; r: number; g: number; b: number }[],
  ): void;
  setField(v: number): void;
  setVertExaggeration(v: number): void;
  setWaveExaggeration(v: number): void;
  setShowSea(v: boolean): void;
  setShowSlabOverlay(v: boolean): void;
  setRestrictToSlab2(v: boolean): void;
  setSimCellSize(v: number): void;
  setSimSpeed(v: number): void;
  setSimAutoSpeed(v: boolean): void;
  startSim(): void;
  stopSim(): void;
}

declare global {
  interface Window {
    createTsunamiModule(): Promise<TsunamiModule>;
  }
}

/** Copies bytes into the wasm heap, runs fn, frees. */
export function withHeapBytes<T>(
  mod: TsunamiModule,
  bytes: Uint8Array,
  fn: (ptr: number, size: number) => T,
): T {
  const ptr = mod._malloc(bytes.length);
  mod.HEAPU8.set(bytes, ptr);
  try {
    return fn(ptr, bytes.length);
  } finally {
    mod._free(ptr);
  }
}

/** Copies several buffers into the wasm heap at once, runs fn, frees all. */
export function withHeapBytesMulti<T>(
  mod: TsunamiModule,
  buffers: Uint8Array[],
  fn: (ptrs: number[], sizes: number[]) => T,
): T {
  const ptrs = buffers.map((b) => {
    const p = mod._malloc(b.length);
    mod.HEAPU8.set(b, p);
    return p;
  });
  try {
    return fn(
      ptrs,
      buffers.map((b) => b.length),
    );
  } finally {
    ptrs.forEach((p) => mod._free(p));
  }
}

// World tiling for on-demand free-hand selections — must match TILE_DEG in
// tools/make_web_data.py.
const TILE_DEG = 6;
const TILE_TILES_X = 60; // 360 / TILE_DEG
const TILE_TILES_Y = 30; // 180 / TILE_DEG

/** URLs of the tiles (tools/make_web_data.py) covering a lon/lat bbox. */
export function tileUrlsForBbox(
  dataBase: string,
  lonMin: number,
  lonMax: number,
  latMin: number,
  latMax: number,
): string[] {
  const clamp = (v: number, lo: number, hi: number) =>
    Math.min(Math.max(v, lo), hi);
  const tx0 = clamp(Math.floor((lonMin + 180) / TILE_DEG), 0, TILE_TILES_X - 1);
  const tx1 = clamp(Math.floor((lonMax + 180) / TILE_DEG), 0, TILE_TILES_X - 1);
  const ty0 = clamp(Math.floor((latMin + 90) / TILE_DEG), 0, TILE_TILES_Y - 1);
  const ty1 = clamp(Math.floor((latMax + 90) / TILE_DEG), 0, TILE_TILES_Y - 1);
  const urls: string[] = [];
  for (let ty = ty0; ty <= ty1; ty++)
    for (let tx = tx0; tx <= tx1; tx++)
      urls.push(`${dataBase}tiles/tile_${tx}_${ty}.bin.gz`);
  return urls;
}

/**
 * Fetches a binary payload. .gz files arrive either already inflated (vite
 * serves them with Content-Encoding: gzip, the browser decompresses
 * transparently) or raw (plain static hosts) — so inflate only when the
 * bytes still carry the gzip magic.
 */
export async function fetchBytes(url: string): Promise<Uint8Array> {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  let bytes = new Uint8Array(await r.arrayBuffer());
  if (bytes.length > 2 && bytes[0] === 0x1f && bytes[1] === 0x8b) {
    const stream = new Blob([bytes.buffer as ArrayBuffer])
      .stream()
      .pipeThrough(new DecompressionStream("gzip"));
    bytes = new Uint8Array(await new Response(stream).arrayBuffer());
  }
  return bytes;
}
