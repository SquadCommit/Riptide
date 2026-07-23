import { Loader2 } from "lucide-react";

import { Badge } from "@/components/ui/badge";
import { SidePanel } from "@/components/panelControls";
import type { Scenario } from "@/lib/tsunami";

const SCENARIO_META: { year: string; place: string }[] = [
  { year: "2011", place: "Tōhoku, Japan" },
  { year: "2004", place: "Sumatra-Andamanen" },
  { year: "1960", place: "Valdivia, Chile" },
  { year: "2010", place: "Maule, Chile" },
  { year: "1964", place: "Alaska" },
];

/** Right panel in the globe state: the one-click historical scenarios. */
export function HistoryPanel({
  scenarios,
  scenarioLoading,
  loadScenario,
}: {
  scenarios: Scenario[];
  scenarioLoading: number | null;
  loadScenario: (idx: number) => void;
}) {
  return (
    <SidePanel side="right">
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
    </SidePanel>
  );
}
