import type { AppSnapshot } from "@/lib/tsunami";

function fmtClock(t: number): string {
  const h = Math.floor(t / 3600);
  const m = Math.floor(t / 60) % 60;
  const s = Math.floor(t) % 60;
  const pad = (v: number) => String(v).padStart(2, "0");
  return `${pad(h)}:${pad(m)}:${pad(s)}`;
}

/** Floating readout while the solver runs: elapsed quake time, steps, rate. */
export function SimHud({ snapshot }: { snapshot: AppSnapshot }) {
  const sim = snapshot.sim;
  if (!sim.running) return null;
  return (
    <div className="pointer-events-none absolute left-1/2 top-20 z-20 -translate-x-1/2">
      <div className="flex items-center gap-4 rounded-2xl border border-border bg-card/70 px-5 py-3 shadow-lg backdrop-blur-xl">
        <span className="relative flex h-2.5 w-2.5">
          <span className="animate-pulse-dot absolute inline-flex h-full w-full rounded-full bg-red-500" />
        </span>
        <div>
          <div className="font-mono text-2xl leading-none text-foreground">
            T+{fmtClock(sim.time ?? 0)}
          </div>
          <div className="mt-1 text-[11px] uppercase tracking-wider text-muted-foreground">
            seit Beben · {Math.round(sim.steps ?? 0).toLocaleString("de-DE")}{" "}
            Schritte
            {sim.autoSpeed && (sim.maxSpeed ?? 0) > 0
              ? ` · ~${Math.round(sim.maxSpeed!)}×`
              : ` · ${Math.round(sim.speed)}×`}
          </div>
        </div>
      </div>
    </div>
  );
}
