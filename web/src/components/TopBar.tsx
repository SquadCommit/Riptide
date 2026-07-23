import { ChevronRight, Waves } from "lucide-react";

import { Badge } from "@/components/ui/badge";
import type { AppSnapshot } from "@/lib/tsunami";
import { cn } from "@/lib/utils";

export function TopBar({ snapshot }: { snapshot: AppSnapshot }) {
  const inRegion = snapshot.state === "region";
  const simRunning = snapshot.sim.running;

  const crumb = (label: string, active: boolean) => (
    <span
      className={cn(
        "text-xs uppercase tracking-widest",
        active ? "text-foreground" : "text-muted-foreground/60",
      )}
    >
      {label}
    </span>
  );

  return (
    <div className="pointer-events-none absolute left-1/2 top-4 z-20 -translate-x-1/2">
      <div className="flex items-center gap-3 rounded-full border border-border bg-card/70 px-5 py-2 shadow-lg backdrop-blur-xl">
        <Waves className="h-4 w-4 text-primary" />
        <span className="text-sm font-semibold tracking-[0.2em]">
          TSUNAMI<span className="text-primary">LAB</span>
        </span>
        {/* Breadcrumb is space-hungry — hide it on narrow (mobile) screens. */}
        <div className="hidden items-center gap-3 sm:flex">
          <div className="mx-1 h-4 w-px bg-border" />
          {crumb("Gebiet", !inRegion)}
          <ChevronRight className="h-3 w-3 text-muted-foreground/50" />
          {crumb("Region", inRegion && !simRunning)}
          <ChevronRight className="h-3 w-3 text-muted-foreground/50" />
          {crumb("Simulation", simRunning)}
        </div>
        {snapshot.slab.available && (
          <>
            <div className="mx-1 hidden h-4 w-px bg-border sm:block" />
            <Badge variant="cyan" className="text-[10px]">
              Slab2 aktiv
            </Badge>
          </>
        )}
      </div>
    </div>
  );
}
