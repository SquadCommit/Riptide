import { useRef } from "react";

import { HoverTooltip } from "@/components/HoverTooltip";
import { Legends } from "@/components/Legends";
import { LoadingScreen } from "@/components/LoadingScreen";
import { RegionPanel } from "@/components/RegionPanel";
import { ScenarioPanel } from "@/components/ScenarioPanel";
import { SimHud } from "@/components/SimHud";
import { TopBar } from "@/components/TopBar";
import { useTsunami } from "@/hooks/useTsunami";

export default function App() {
  const viewRef = useRef<HTMLDivElement>(null);
  const {
    mod,
    boot,
    snapshot,
    scenarios,
    scenarioLoading,
    loadScenario,
    selectionLoading,
    loadSelection,
  } = useTsunami(viewRef);

  return (
    <div ref={viewRef} className="relative h-full w-full">
      {/* The wasm renderer binds "#canvas" at boot — must exist up front. */}
      <canvas id="canvas" tabIndex={0} />

      <LoadingScreen boot={boot} />

      {boot.phase === "ready" && snapshot && (
        <>
          <TopBar snapshot={snapshot} />
          <SimHud snapshot={snapshot} />
          {snapshot.state === "globe" ? (
            <ScenarioPanel
              mod={mod}
              snapshot={snapshot}
              scenarios={scenarios}
              scenarioLoading={scenarioLoading}
              loadScenario={loadScenario}
              selectionLoading={selectionLoading}
              loadSelection={loadSelection}
            />
          ) : (
            <RegionPanel mod={mod} snapshot={snapshot} />
          )}
          <Legends snapshot={snapshot} />
          <HoverTooltip mod={mod} snapshot={snapshot} viewRef={viewRef} />
        </>
      )}
    </div>
  );
}
