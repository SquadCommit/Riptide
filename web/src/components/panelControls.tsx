import { Switch } from "@/components/ui/switch";

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

/** Shared shell for the floating side panels (left = data/sim, right =
 * source). `side` picks the screen edge; content scrolls only as a fallback
 * on very short viewports. */
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
    <div
      className={`pointer-events-auto absolute top-20 z-20 flex max-h-[calc(100%-7rem)] w-80 flex-col rounded-2xl border border-border bg-card/70 shadow-2xl backdrop-blur-xl ${
        side === "left" ? "left-5" : "right-5"
      }`}
    >
      <div className="panel-scroll flex-1 space-y-5 overflow-y-auto p-5">
        {children}
      </div>
      {footer && <div className="border-t border-border p-3">{footer}</div>}
    </div>
  );
}
