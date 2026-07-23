import { useState } from "react";
import { ChevronDown, SlidersHorizontal } from "lucide-react";

import { cn } from "@/lib/utils";

/**
 * Mobile-only bottom sheet. Below the `lg` breakpoint the two floating side
 * panels don't fit next to the 3d view, so their cards stack here in a single
 * collapsible, scrollable sheet — keeping the globe/terrain visible above it.
 */
export function MobileDock({
  title,
  children,
}: {
  title: string;
  children: React.ReactNode;
}) {
  const [open, setOpen] = useState(true);

  return (
    <div className="pointer-events-none fixed inset-x-0 bottom-0 z-30 flex flex-col px-2 pb-2">
      <div
        className={cn(
          "pointer-events-auto flex flex-col overflow-hidden rounded-2xl border border-border bg-card/85 shadow-2xl backdrop-blur-xl transition-[max-height] duration-200",
          open ? "max-h-[68vh]" : "max-h-12",
        )}
      >
        <button
          onClick={() => setOpen((v) => !v)}
          className="flex shrink-0 items-center justify-between px-4 py-3"
        >
          <span className="flex items-center gap-2 text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
            <SlidersHorizontal className="h-3.5 w-3.5" />
            {title}
          </span>
          <ChevronDown
            className={cn(
              "h-4 w-4 text-muted-foreground transition-transform",
              open ? "" : "rotate-180",
            )}
          />
        </button>
        <div className="panel-scroll min-h-0 flex-1 space-y-3 overflow-y-auto px-2 pb-2">
          {children}
        </div>
      </div>
    </div>
  );
}
