import { Badge } from "@/components/ui/badge";
import { Separator } from "@/components/ui/separator";
import { Slider } from "@/components/ui/slider";
import { Row, SidePanel, SwitchRow, log10 } from "@/components/panelControls";
import type { AppSnapshot, TsunamiModule } from "@/lib/tsunami";
import { cn } from "@/lib/utils";

/**
 * Right panel in the region state: everything that defines the earthquake
 * displacement (source magnitude / epicentre / slab restriction) plus how
 * the field is rendered.
 */
export function SourcePanel({
  mod,
  snapshot,
}: {
  mod: React.RefObject<TsunamiModule | null>;
  snapshot: AppSnapshot;
}) {
  const m = mod.current;
  const r = snapshot.region!;
  const q = snapshot.quake;
  const running = snapshot.sim.running;

  return (
    <SidePanel side="right">
      {/* Erdbebenquelle */}
      <div className="space-y-3">
        <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
          Erdbebenquelle
        </h2>
        <div className="flex items-end justify-between">
          <span className="text-xs text-muted-foreground">
            Momenten-Magnitude
          </span>
          <span className="font-mono text-3xl leading-none text-primary">
            {q.mw.toFixed(1)}
          </span>
        </div>
        <Slider
          min={6}
          max={9.5}
          step={0.1}
          value={[q.mw]}
          disabled={running}
          onValueChange={([v]) => m?.setMw(v)}
          onValueCommit={() => m?.commitMw()}
        />
        {q.hasClick ? (
          <div className="space-y-2 rounded-xl border border-border bg-secondary/40 p-3">
            <div className="flex items-center justify-between">
              <span className="font-mono text-xs">
                {q.epiLat.toFixed(2)}°, {q.epiLon.toFixed(2)}°
              </span>
              {q.inCoverage ? (
                <Badge variant="cyan" className="text-[10px]">
                  {q.slabRegion || "Slab2"}
                </Badge>
              ) : (
                <Badge variant="amber" className="text-[10px]">
                  Fallback
                </Badge>
              )}
            </div>
            <div className="grid grid-cols-3 gap-2 font-mono text-xs">
              <span>{(q.slip ?? 0).toFixed(1)} m</span>
              <span>{((q.length ?? 0) / 1000).toFixed(0)} km</span>
              <span>{((q.width ?? 0) / 1000).toFixed(0)} km</span>
            </div>
            <div className="grid grid-cols-3 gap-2 text-[9px] uppercase tracking-wide text-muted-foreground">
              <span>Versatz</span>
              <span>Länge</span>
              <span>Breite</span>
            </div>
            {q.inCoverage && (
              <div className="font-mono text-[11px] text-muted-foreground">
                Tiefe {((q.slabDepth ?? 0) / 1000).toFixed(0)} km · Streichen{" "}
                {(q.slabStrike ?? 0).toFixed(0)}° · Dip{" "}
                {(q.slabDip ?? 0).toFixed(0)}°
              </div>
            )}
            <div className="text-[10px] text-muted-foreground">
              {q.ifaceScaling
                ? "Skalierung: Strasser et al. 2010 (Interface)"
                : "Skalierung: Wells & Coppersmith 1994"}
            </div>
          </div>
        ) : (
          <p className="text-xs leading-relaxed text-muted-foreground">
            Klick auf das Terrain setzt das Epizentrum — grüner Ring =
            Subduktionszone getroffen.
          </p>
        )}
        {q.computing && (
          <div className="animate-pulse text-xs text-amber-300">
            Okada-Displacement wird berechnet …
          </div>
        )}
        {snapshot.slab.available && (
          <SwitchRow
            label="Nur in Subduktionszonen auslösen"
            checked={snapshot.slab.restrict}
            onChange={(v) => m?.setRestrictToSlab2(v)}
          />
        )}
      </div>

      <Separator />

      {/* Darstellung */}
      <div className="space-y-3">
        <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
          Darstellung
        </h2>
        <div className="grid grid-cols-2 gap-2">
          {(["Bathymetrie", "Displacement"] as const).map((label, i) => (
            <button
              key={label}
              onClick={() => m?.setField(i)}
              className={cn(
                "rounded-lg border px-3 py-1.5 text-xs transition-colors",
                r.field === i
                  ? "border-primary/50 bg-primary/15 text-primary"
                  : "border-border bg-secondary/40 text-muted-foreground hover:bg-accent",
              )}
            >
              {label}
            </button>
          ))}
        </div>
        <Row label="Terrain-Überhöhung" value={`${r.vertExag.toFixed(0)}×`}>
          <Slider
            min={1}
            max={100}
            step={1}
            value={[r.vertExag]}
            onValueChange={([v]) => m?.setVertExaggeration(v)}
          />
        </Row>
        <Row label="Wellen-Überhöhung" value={`${r.waveExag.toFixed(0)}×`}>
          <Slider
            min={0}
            max={3.7}
            step={0.01}
            value={[log10.to(r.waveExag)]}
            onValueChange={([v]) => m?.setWaveExaggeration(Math.pow(10, v))}
          />
        </Row>
        <SwitchRow
          label="Meeresspiegel"
          checked={r.showSea}
          onChange={(v) => m?.setShowSea(v)}
        />
        {snapshot.slab.available && (
          <SwitchRow
            label="Subduktionszonen zeigen"
            checked={snapshot.slab.showOverlay}
            onChange={(v) => m?.setShowSlabOverlay(v)}
          />
        )}
      </div>
    </SidePanel>
  );
}
