// Validated dark-mode categorical palette (see the dataviz skill) — checked
// against this app's actual card surface (~#101924): lightness band, CVD
// adjacent-pair separation (>=8 dE), normal-vision floor (>=15 dE) and
// contrast all pass for the full 8-slot set on that surface.
const STATION_COLORS = [
  "#3987e5", // blue
  "#d95926", // orange
  "#199e70", // aqua
  "#c98500", // yellow
  "#d55181", // magenta
  "#008300", // green
  "#9085e9", // violet
  "#e66767", // red
];

// Colour follows the station's name, not its position in the list, so a
// removal never repaints the survivors (see references/anti-patterns.md,
// "color follows the entity, never its rank") — and the chart line and the
// 3d marker (setStationMarkers) always agree, since both read this.
export function stationColor(name: string): string {
  let hash = 0;
  for (let i = 0; i < name.length; i++)
    hash = (hash * 31 + name.charCodeAt(i)) | 0;
  return STATION_COLORS[Math.abs(hash) % STATION_COLORS.length];
}

/** #rrggbb -> {r,g,b} in 0..1, for the wasm marker uniform. */
export function hexToRgb01(hex: string): { r: number; g: number; b: number } {
  const n = parseInt(hex.slice(1), 16);
  return {
    r: ((n >> 16) & 0xff) / 255,
    g: ((n >> 8) & 0xff) / 255,
    b: (n & 0xff) / 255,
  };
}
