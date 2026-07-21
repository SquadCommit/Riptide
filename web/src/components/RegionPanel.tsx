import { ArrowLeft, Play, Square } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { Slider } from "@/components/ui/slider";
import { Row, SidePanel, SwitchRow, log10 } from "@/components/panelControls";
import { StationsPanel } from "@/components/StationsPanel";
import type { AppSnapshot, Station, TsunamiModule } from "@/lib/tsunami";

/**
 * Left panel in the region state: the loaded region's data, the live
 * simulation controls and the virtual gauges. The earthquake source and
 * rendering options live in the right-hand SourcePanel.
 */
export function RegionPanel({
  mod,
  snapshot,
  stations,
  togglePlacingStation,
  removeStation,
  renameStation,
  clearStations,
}: {
  mod: React.RefObject<TsunamiModule | null>;
  snapshot: AppSnapshot;
  stations: Station[];
  togglePlacingStation: (v: boolean) => void;
  removeStation: (idx: number) => void;
  renameStation: (idx: number, name: string) => void;
  clearStations: () => void;
}) {
  const m = mod.current;
  const r = snapshot.region!;
  const q = snapshot.quake;
  const sim = snapshot.sim;
  const running = sim.running;

  return (
    <SidePanel
      side="left"
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
        <Row label="Zeitraffer" value={`${Math.round(sim.speed)}×`}>
          <Slider
            min={0}
            max={3.3}
            step={0.01}
            disabled={sim.autoSpeed}
            value={[log10.to(sim.speed)]}
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

      <Separator />

      <StationsPanel
        snapshot={snapshot}
        stations={stations}
        togglePlacingStation={togglePlacingStation}
        removeStation={removeStation}
        renameStation={renameStation}
        clearStations={clearStations}
      />

      {snapshot.error && (
        <div className="rounded-lg border border-red-500/30 bg-red-500/10 px-3 py-2 text-xs text-red-300">
          {snapshot.error}
        </div>
      )}
    </SidePanel>
  );
}
