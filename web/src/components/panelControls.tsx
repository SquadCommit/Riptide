import { Switch } from "@/components/ui/switch";
import { cn } from "@/lib/utils";

// Slider values that span orders of magnitude (cell size, speed, wave exag.)
// are edited in log10 space; guard against log(0).
export const log10 = { to: (v: number) => Math.log10(Math.max(1, v)) };

/** Labelled control row with an optional right-aligned mono value. */
export function Row({
  label,
  value,
  children,
}: {
  label: string;
  value?: string;
  children?: React.ReactNode;
}) {
  return (
    <div>
      <div className="mb-1.5 flex items-baseline justify-between">
        <span className="text-xs text-muted-foreground">{label}</span>
        {value && <span className="font-mono text-xs">{value}</span>}
      </div>
      {children}
    </div>
  );
}

/** Label + toggle on one line. */
export function SwitchRow({
  label,
  checked,
  onChange,
}: {
  label: string;
  checked: boolean;
  onChange: (v: boolean) => void;
}) {
  return (
    <label className="flex cursor-pointer items-center justify-between py-0.5">
      <span className="text-xs text-muted-foreground">{label}</span>
      <Switch checked={checked} onCheckedChange={onChange} />
    </label>
  );
}

/** A floating glass card with a scrollable body and an optional pinned
 * footer. Positioning is left to the caller via `className` (either a
 * SidePanel wrapper or a flex container). */
export function Panel({
  children,
  footer,
  className,
}: {
  children: React.ReactNode;
  footer?: React.ReactNode;
  className?: string;
}) {
  return (
    <div
      className={cn(
        "pointer-events-auto flex min-h-0 flex-col rounded-2xl border border-border bg-card/70 shadow-2xl backdrop-blur-xl",
        className,
      )}
    >
      <div className="panel-scroll min-h-0 flex-1 space-y-5 overflow-y-auto p-5">
        {children}
      </div>
      {footer && <div className="border-t border-border p-3">{footer}</div>}
    </div>
  );
}

/** Panel anchored to a screen edge (top-aligned). Content scrolls only as a
 * fallback on very short viewports. */
export function SidePanel({
  side,
  children,
  footer,
}: {
  side: "left" | "right";
  children: React.ReactNode;
  footer?: React.ReactNode;
}) {
  return (
    <Panel
      footer={footer}
      className={cn(
        "absolute top-5 z-20 max-h-[calc(100%-2.5rem)] w-80",
        side === "left" ? "left-5" : "right-5",
      )}
    >
      {children}
    </Panel>
  );
}
