import { Loader2, MapPinned, X } from "lucide-react";

import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { SwitchRow } from "@/components/panelControls";
import type { AppSnapshot, Scenario, TsunamiModule } from "@/lib/tsunami";

const SCENARIO_META: { year: string; place: string }[] = [
  { year: "2011", place: "Tōhoku, Japan" },
  { year: "2004", place: "Sumatra-Andamanen" },
  { year: "1960", place: "Valdivia, Chile" },
  { year: "2010", place: "Maule, Chile" },
  { year: "1964", place: "Alaska" },
];

/** Left panel in the globe state: historical scenarios + manual selection. */
export function ScenarioPanel({
  mod,
  snapshot,
  scenarios,
  scenarioLoading,
  loadScenario,
  selectionLoading,
  loadSelection,
}: {
  mod: React.RefObject<TsunamiModule | null>;
  snapshot: AppSnapshot;
  scenarios: Scenario[];
  scenarioLoading: number | null;
  loadScenario: (idx: number) => void;
  selectionLoading: boolean;
  loadSelection: () => void;
}) {
  const sel = snapshot.selection;
  return (
    <div className="pointer-events-auto absolute left-5 top-20 z-20 flex max-h-[calc(100%-7rem)] w-80 flex-col rounded-2xl border border-border bg-card/70 shadow-2xl backdrop-blur-xl">
      <div className="panel-scroll flex-1 space-y-5 overflow-y-auto p-5">
        <div>
          <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
            Historische Ereignisse
          </h2>
          <div className="mt-3 space-y-2">
            {scenarios.map((s, i) => (
              <button
                key={s.name}
                onClick={() => loadScenario(i)}
                disabled={scenarioLoading !== null}
                className="group flex w-full items-center justify-between rounded-xl border border-border bg-secondary/40 px-3.5 py-2.5 text-left transition-colors hover:border-primary/40 hover:bg-accent disabled:opacity-50"
              >
                <div>
                  <div className="text-sm font-medium">
                    {SCENARIO_META[i]?.place ?? s.name}
                  </div>
                  <div className="text-xs text-muted-foreground">
                    {SCENARIO_META[i]?.year} · {s.epiLat.toFixed(1)}°,{" "}
                    {s.epiLon.toFixed(1)}°
                  </div>
                </div>
                {scenarioLoading === i ? (
                  <Loader2 className="h-4 w-4 animate-spin text-primary" />
                ) : (
                  <Badge variant="amber" className="font-mono">
                    M{s.mw.toFixed(1)}
                  </Badge>
                )}
              </button>
            ))}
          </div>
        </div>

        <Separator />

        <div>
          <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
            Eigenes Gebiet
          </h2>
          <p className="mt-2 text-xs leading-relaxed text-muted-foreground">
            Rechteck auf der Karte ziehen oder eine Subduktionszone anklicken
            — ein Klick auf die farbigen Zonen schlägt ein passendes Gebiet
            vor.
          </p>
          {sel.has ? (
            <div className="mt-3 space-y-2.5">
              <div className="rounded-lg border border-primary/30 bg-primary/10 px-3 py-2 font-mono text-xs">
                {sel.lonMin!.toFixed(1)}° … {sel.lonMax!.toFixed(1)}° ·{" "}
                {sel.latMin!.toFixed(1)}° … {sel.latMax!.toFixed(1)}°
              </div>
              <div className="flex gap-2">
                <Button
                  className="flex-1"
                  onClick={loadSelection}
                  disabled={selectionLoading}
                >
                  {selectionLoading ? (
                    <Loader2 className="animate-spin" />
                  ) : (
                    <MapPinned />
                  )}
                  Region laden
                </Button>
                <Button
                  variant="outline"
                  size="icon"
                  disabled={selectionLoading}
                  onClick={() => mod.current?.clearSelection()}
                >
                  <X />
                </Button>
              </div>
              <p className="text-[11px] leading-relaxed text-muted-foreground">
                Lädt automatisch die passenden Gelände-Kacheln nach (~1,4 km
                Auflösung — historische Ereignisse nutzen noch feinere,
                fest hinterlegte Daten).
              </p>
            </div>
          ) : (
            <div className="mt-3 rounded-lg border border-dashed border-border px-3 py-2 text-center text-xs text-muted-foreground">
              keine Auswahl
            </div>
          )}
        </div>

        {snapshot.slab.available && (
          <>
            <Separator />
            <div className="space-y-3">
              <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
                Darstellung
              </h2>
              <SwitchRow
                label="Subduktionszonen zeigen"
                checked={snapshot.slab.showOverlay}
                onChange={(v) => mod.current?.setShowSlabOverlay(v)}
              />
            </div>
          </>
        )}

        <div className="text-[10px] leading-relaxed text-muted-foreground/70">
          Bathymetrie: GEBCO 2026 · Subduktionsgeometrie: USGS Slab2 ·
          Steuerung: Ziehen = Auswahl, Mittelklick/WASD = Karte, Scrollen =
          Zoom
        </div>
      </div>
    </div>
  );
}
