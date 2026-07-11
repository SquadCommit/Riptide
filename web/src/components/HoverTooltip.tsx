import { useEffect, useRef, useState } from "react";

import type { AppSnapshot, HoverInfo, TsunamiModule } from "@/lib/tsunami";
import { cn } from "@/lib/utils";

interface Hover {
  x: number;
  y: number;
  info: HoverInfo;
}

/**
 * Cursor-attached geo readout: subduction-zone geometry on the globe, and a
 * click-feedback ring (green = fault possible, red = restricted, grey =
 * displacement busy) in the region view. Purely visual — all input still
 * goes to the canvas underneath.
 */
export function HoverTooltip({
  mod,
  snapshot,
  viewRef,
}: {
  mod: React.RefObject<TsunamiModule | null>;
  snapshot: AppSnapshot;
  viewRef: React.RefObject<HTMLDivElement | null>;
}) {
  const [hover, setHover] = useState<Hover | null>(null);
  const lastQuery = useRef(0);

  useEffect(() => {
    const onMove = (e: MouseEvent) => {
      const m = mod.current;
      const view = viewRef.current;
      if (!m || !view) return;
      // Only while the cursor is actually over the canvas (not a panel).
      const el = document.elementFromPoint(e.clientX, e.clientY);
      if (el && el.id !== "canvas") {
        setHover(null);
        return;
      }
      const now = performance.now();
      if (now - lastQuery.current < 33) return; // ~30 Hz is plenty
      lastQuery.current = now;
      const r = view.getBoundingClientRect();
      const info = m.getHoverInfo(e.clientX - r.left, e.clientY - r.top);
      setHover(
        info.valid ? { x: e.clientX, y: e.clientY, info } : null,
      );
    };
    const onLeave = () => setHover(null);
    window.addEventListener("mousemove", onMove);
    document.addEventListener("mouseleave", onLeave);
    return () => {
      window.removeEventListener("mousemove", onMove);
      document.removeEventListener("mouseleave", onLeave);
    };
  }, [mod, viewRef]);

  if (!hover) return null;
  const { info } = hover;
  const inRegion = snapshot.state === "region";
  const slab = info.slab;
  const slabValid = !!slab?.valid;

  // Region view: feedback ring colour matches the retired native app.
  const busy = snapshot.quake.computing;
  const restricted = snapshot.slab.restrict;
  const ringColor = busy
    ? "border-neutral-400"
    : slabValid
      ? "border-emerald-400"
      : restricted && snapshot.slab.available
        ? "border-red-400"
        : "border-amber-300";

  return (
    <div
      className="pointer-events-none fixed z-30"
      style={{ left: hover.x, top: hover.y }}
    >
      {inRegion && !snapshot.sim.running && (
        <div
          className={cn(
            "absolute -left-3 -top-3 h-6 w-6 rounded-full border-2",
            ringColor,
          )}
        />
      )}
      {(slabValid || !inRegion) && (
        <div className="absolute left-4 top-4 min-w-40 rounded-lg border border-border bg-popover/90 px-3 py-2 shadow-xl backdrop-blur">
          <div className="font-mono text-[11px] text-muted-foreground">
            {info.lon!.toFixed(2)}° / {info.lat!.toFixed(2)}°
          </div>
          {slabValid && (
            <>
              <div className="mt-1 text-xs font-medium text-amber-300">
                {slab!.region || "Subduktionszone"}
              </div>
              <div className="mt-1 grid grid-cols-3 gap-2 font-mono text-[11px] text-foreground/90">
                <span>{(slab!.depth! / 1000).toFixed(0)} km</span>
                <span>{slab!.strike!.toFixed(0)}°</span>
                <span>{slab!.dip!.toFixed(0)}°</span>
              </div>
              <div className="grid grid-cols-3 gap-2 text-[9px] uppercase tracking-wide text-muted-foreground">
                <span>Tiefe</span>
                <span>Streichen</span>
                <span>Einfallen</span>
              </div>
              {!inRegion && (
                <div className="mt-1.5 text-[10px] text-muted-foreground">
                  Klick: Gebiets-Vorschlag
                </div>
              )}
            </>
          )}
        </div>
      )}
    </div>
  );
}
