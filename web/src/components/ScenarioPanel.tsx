import { Loader2, MapPinned, X } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { SidePanel, SwitchRow } from "@/components/panelControls";
import type { AppSnapshot, TsunamiModule } from "@/lib/tsunami";

/**
 * Left panel in the globe state: manual region selection and the globe
 * rendering options. The historical scenarios live in the right-hand
 * HistoryPanel.
 */
export function ScenarioPanel({
  mod,
  snapshot,
  selectionLoading,
  loadSelection,
}: {
  mod: React.RefObject<TsunamiModule | null>;
  snapshot: AppSnapshot;
  selectionLoading: boolean;
  loadSelection: () => void;
}) {
  const sel = snapshot.selection;
  return (
    <SidePanel side="left">
      <div>
        <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
          Eigenes Gebiet
        </h2>
        <p className="mt-2 text-xs leading-relaxed text-muted-foreground">
          Rechteck auf der Karte ziehen oder eine Subduktionszone anklicken —
          ein Klick auf die farbigen Zonen schlägt ein passendes Gebiet vor.
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
              Auflösung — historische Ereignisse nutzen noch feinere, fest
              hinterlegte Daten).
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
        Bathymetrie: GEBCO 2026 · Subduktionsgeometrie: USGS Slab2 · Steuerung:
        Ziehen = Auswahl, Mittelklick/WASD = Karte, Scrollen = Zoom
      </div>
    </SidePanel>
  );
}
