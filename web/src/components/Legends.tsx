import type { AppSnapshot } from "@/lib/tsunami";

// Colour stops mirror the shader colormaps (region_terrain.frag /
// region_water.frag) and the slab ramp in GlobeView.cpp.
const TERRAIN = [
  { t: 0.0, c: "rgb(8,33,97)" },
  { t: 0.3, c: "rgb(23,71,148)" },
  { t: 0.5, c: "rgb(46,115,194)" },
  { t: 0.58, c: "rgb(84,158,212)" },
  { t: 0.6, c: "rgb(130,189,219)" },
  { t: 0.601, c: "rgb(69,140,69)" },
  { t: 0.61, c: "rgb(115,168,82)" },
  { t: 0.63, c: "rgb(184,186,99)" },
  { t: 0.66, c: "rgb(199,168,115)" },
  { t: 0.72, c: "rgb(158,122,92)" },
  { t: 0.85, c: "rgb(122,102,92)" },
  { t: 1.0, c: "rgb(242,242,245)" },
];

const WAVE = [
  { t: 0.0, c: "rgb(58,5,55)" },
  { t: 0.125, c: "rgb(140,13,140)" },
  { t: 0.25, c: "rgb(107,31,199)" },
  { t: 0.375, c: "rgb(56,71,209)" },
  { t: 0.5, c: "rgb(33,87,204)" },
  { t: 0.6, c: "rgb(26,158,224)" },
  { t: 0.7, c: "rgb(51,204,107)" },
  { t: 0.8, c: "rgb(242,224,51)" },
  { t: 0.9, c: "rgb(247,133,31)" },
  { t: 1.0, c: "rgb(209,26,23)" },
];

const SLAB = [
  { t: 0.0, c: "rgb(255,195,60)" },
  { t: 0.35, c: "rgb(255,95,40)" },
  { t: 0.7, c: "rgb(205,45,115)" },
  { t: 1.0, c: "rgb(115,35,165)" },
];

function gradient(stops: { t: number; c: string }[]) {
  return `linear-gradient(to right, ${stops
    .map((s) => `${s.c} ${(s.t * 100).toFixed(1)}%`)
    .join(", ")})`;
}

function LegendBar({
  title,
  stops,
  left,
  right,
}: {
  title: string;
  stops: { t: number; c: string }[];
  left: string;
  right: string;
}) {
  return (
    <div className="w-60 rounded-xl border border-border bg-card/70 px-4 py-3 shadow-lg backdrop-blur-xl">
      <div className="mb-2 text-[11px] uppercase tracking-wider text-muted-foreground">
        {title}
      </div>
      <div
        className="h-2.5 w-full rounded-full"
        style={{ background: gradient(stops) }}
      />
      <div className="mt-1.5 flex justify-between font-mono text-[11px] text-muted-foreground">
        <span>{left}</span>
        <span>{right}</span>
      </div>
    </div>
  );
}

export function Legends({ snapshot }: { snapshot: AppSnapshot }) {
  const inRegion = snapshot.state === "region";
  const bathyField = inRegion && snapshot.region?.field === 0;
  const anom = snapshot.region?.waterAnom ?? 1;
  return (
    <div className="pointer-events-none absolute bottom-5 right-5 z-20 hidden flex-col gap-2.5 lg:flex">
      {snapshot.sim.running && bathyField && (
        <LegendBar
          title="Wellenhöhe (Anomalie)"
          stops={WAVE}
          left={`-${anom.toFixed(2)} m`}
          right={`+${anom.toFixed(2)} m`}
        />
      )}
      {bathyField && (
        <LegendBar
          title="Höhe / Tiefe"
          stops={TERRAIN}
          left="-6000 m"
          right="+4000 m"
        />
      )}
      {!inRegion && snapshot.slab.available && snapshot.slab.showOverlay && (
        <LegendBar
          title="Slab-Tiefe (Subduktion)"
          stops={SLAB}
          left="0 km (Graben)"
          right="660 km"
        />
      )}
    </div>
  );
}
