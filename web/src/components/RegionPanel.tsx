import { ArrowLeft, Play, Square } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { Slider } from "@/components/ui/slider";
import { Panel, Row, SwitchRow, log10 } from "@/components/panelControls";
import type { AppSnapshot, TsunamiModule } from "@/lib/tsunami";

/**
 * Top-left card in the region state: the loaded region's data and the live
 * simulation controls. The gauges live in the StationsCard below it, the
 * earthquake source and rendering options in the right-hand SourcePanel.
 */
export function RegionPanel({
  mod,
  snapshot,
}: {
  mod: React.RefObject<TsunamiModule | null>;
  snapshot: AppSnapshot;
}) {
  const m = mod.current;
  const r = snapshot.region!;
  const q = snapshot.quake;
  const sim = snapshot.sim;
  const running = sim.running;

  // Zeitraffer readout. maxSpeed is the live rate the hardware can sustain
  // (only known while running, and it drifts a little each poll):
  //  - auto mode  -> track maxSpeed on the slider + value, since the solver
  //    is being driven at exactly that rate.
  //  - manual mode -> if the setpoint is above maxSpeed the run is capped, so
  //    surface the effective rate like the cell-size "eff." hint does.
  const maxSpeed = sim.maxSpeed ?? 0;
  const shownSpeed = sim.autoSpeed && maxSpeed > 0 ? maxSpeed : sim.speed;
  const speedCapped =
    running && !sim.autoSpeed && maxSpeed > 0 && sim.speed > maxSpeed * 1.05;
  const speedValue = sim.autoSpeed
    ? maxSpeed > 0
      ? `auto · ~${Math.round(maxSpeed)}×`
      : "auto"
    : speedCapped
      ? `${Math.round(sim.speed)}× → eff. ~${Math.round(maxSpeed)}×`
      : `${Math.round(sim.speed)}×`;

  return (
    <Panel
      className="w-80"
      footer={
        <Button
          variant="ghost"
          size="sm"
          className="w-full"
          disabled={q.computing}
          onClick={() => m?.backToGlobe()}
        >
          <ArrowLeft /> Zur Gebietsauswahl
        </Button>
      }
    >
      <div>
        <div className="flex items-center justify-between">
          <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
            Region
          </h2>
          <span className="font-mono text-[11px] text-muted-foreground">
            {r.gridW} × {r.gridH}
          </span>
        </div>
        <div className="mt-1.5 font-mono text-xs text-foreground/80">
          {r.lonMin.toFixed(1)}° … {r.lonMax.toFixed(1)}° ·{" "}
          {r.latMin.toFixed(1)}° … {r.latMax.toFixed(1)}°
        </div>
      </div>

      <Separator />

      {/* Simulation */}
      <div className="space-y-3">
        <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
          Simulation
        </h2>
        <Row
          label="Zellgröße"
          value={`${Math.round(sim.cellSize)} m${
            (sim.effCellSize ?? 0) > sim.cellSize * 1.01
              ? ` → eff. ${Math.round(sim.effCellSize!)} m`
              : ""
          }`}
        >
          <Slider
            min={1.7}
            max={4.3}
            step={0.01}
            disabled={running}
            value={[log10.to(sim.cellSize)]}
            onValueChange={([v]) => m?.setSimCellSize(Math.pow(10, v))}
          />
        </Row>
        {sim.previewNx && (
          <div className="font-mono text-[11px] text-muted-foreground">
            Gitter {sim.previewNx} × {sim.previewNy} Zellen
          </div>
        )}
        <Row label="Zeitraffer" value={speedValue}>
          <Slider
            min={0}
            max={3.3}
            step={0.01}
            disabled={sim.autoSpeed}
            value={[Math.min(3.3, log10.to(shownSpeed))]}
            onValueChange={([v]) => m?.setSimSpeed(Math.pow(10, v))}
          />
        </Row>
        <SwitchRow
          label="Automatisch (CPU-Limit)"
          checked={sim.autoSpeed}
          onChange={(v) => m?.setSimAutoSpeed(v)}
        />
        {running ? (
          <Button
            variant="destructive"
            className="w-full"
            onClick={() => m?.stopSim()}
          >
            <Square /> Simulation stoppen
          </Button>
        ) : (
          <Button
            className="w-full"
            disabled={q.computing}
            onClick={() => m?.startSim()}
          >
            <Play /> Simulation starten
          </Button>
        )}
      </div>

      {snapshot.error && (
        <div className="rounded-lg border border-red-500/30 bg-red-500/10 px-3 py-2 text-xs text-red-300">
          {snapshot.error}
        </div>
      )}
    </Panel>
  );
}
