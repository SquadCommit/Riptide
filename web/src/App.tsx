import { useRef } from "react";

import { HistoryPanel } from "@/components/HistoryPanel";
import { HoverTooltip } from "@/components/HoverTooltip";
import { Legends } from "@/components/Legends";
import { LoadingScreen } from "@/components/LoadingScreen";
import { MobileDock } from "@/components/MobileDock";
import { RegionPanel } from "@/components/RegionPanel";
import { ScenarioPanel } from "@/components/ScenarioPanel";
import { SimHud } from "@/components/SimHud";
import { SourcePanel } from "@/components/SourcePanel";
import { StationsCard } from "@/components/StationsCard";
import { TopBar } from "@/components/TopBar";
import { useIsMobile } from "@/hooks/useIsMobile";
import { useTsunami } from "@/hooks/useTsunami";

export default function App() {
  const viewRef = useRef<HTMLDivElement>(null);
  const isMobile = useIsMobile();
  const {
    mod,
    boot,
    snapshot,
    scenarios,
    scenarioLoading,
    loadScenario,
    selectionLoading,
    loadSelection,
    stations,
    togglePlacingStation,
    removeStation,
    renameStation,
    clearStations,
  } = useTsunami(viewRef);

  const inGlobe = snapshot?.state === "globe";

  return (
    <div ref={viewRef} className="relative h-full w-full">
      {/* The wasm renderer binds "#canvas" at boot — must exist up front. */}
      <canvas
        id="canvas"
        tabIndex={0}
        className={snapshot?.placingStation ? "cursor-crosshair" : undefined}
      />

      <LoadingScreen boot={boot} />

      {boot.phase === "ready" && snapshot && (
        <>
          <TopBar snapshot={snapshot} />
          <SimHud snapshot={snapshot} />

          {isMobile ? (
            <MobileDock title={inGlobe ? "Szenarien & Gebiet" : "Simulation & Quelle"}>
              {inGlobe ? (
                <>
                  <HistoryPanel
                    scenarios={scenarios}
                    scenarioLoading={scenarioLoading}
                    loadScenario={loadScenario}
                  />
                  <ScenarioPanel
                    mod={mod}
                    snapshot={snapshot}
                    selectionLoading={selectionLoading}
                    loadSelection={loadSelection}
                  />
                </>
              ) : (
                <>
                  <RegionPanel mod={mod} snapshot={snapshot} />
                  <SourcePanel mod={mod} snapshot={snapshot} />
                  <StationsCard
                    snapshot={snapshot}
                    stations={stations}
                    togglePlacingStation={togglePlacingStation}
                    removeStation={removeStation}
                    renameStation={renameStation}
                    clearStations={clearStations}
                  />
                </>
              )}
            </MobileDock>
          ) : inGlobe ? (
            <>
              <ScenarioPanel
                mod={mod}
                snapshot={snapshot}
                selectionLoading={selectionLoading}
                loadSelection={loadSelection}
              />
              <HistoryPanel
                scenarios={scenarios}
                scenarioLoading={scenarioLoading}
                loadScenario={loadScenario}
              />
            </>
          ) : (
            <>
              {/* Left column: region controls on top, gauges filling below. */}
              <div className="pointer-events-none absolute bottom-5 left-5 top-5 z-20 flex w-80 flex-col gap-3">
                <RegionPanel mod={mod} snapshot={snapshot} />
                <StationsCard
                  snapshot={snapshot}
                  stations={stations}
                  togglePlacingStation={togglePlacingStation}
                  removeStation={removeStation}
                  renameStation={renameStation}
                  clearStations={clearStations}
                />
              </div>
              <SourcePanel mod={mod} snapshot={snapshot} />
            </>
          )}

          <Legends snapshot={snapshot} />
          <HoverTooltip mod={mod} snapshot={snapshot} viewRef={viewRef} />
        </>
      )}
    </div>
  );
}
