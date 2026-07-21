import { Panel } from "@/components/panelControls";
import { StationsPanel } from "@/components/StationsPanel";
import type { AppSnapshot, Station } from "@/lib/tsunami";

/**
 * Bottom-left card in the region state: the virtual gauges and their live
 * eta(t) chart, split out of the region controls into its own panel. Fills
 * the remaining height of the left column and scrolls internally.
 */
export function StationsCard({
  snapshot,
  stations,
  togglePlacingStation,
  removeStation,
  renameStation,
  clearStations,
}: {
  snapshot: AppSnapshot;
  stations: Station[];
  togglePlacingStation: (v: boolean) => void;
  removeStation: (idx: number) => void;
  renameStation: (idx: number, name: string) => void;
  clearStations: () => void;
}) {
  return (
    <Panel className="min-h-0 w-80 flex-1">
      <StationsPanel
        snapshot={snapshot}
        stations={stations}
        togglePlacingStation={togglePlacingStation}
        removeStation={removeStation}
        renameStation={renameStation}
        clearStations={clearStations}
      />
    </Panel>
  );
}
