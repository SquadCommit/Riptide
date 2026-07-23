import { MapPin, Radio, X } from "lucide-react";
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";

import { Button } from "@/components/ui/button";
import { stationColor } from "@/lib/stationColor";
import type { AppSnapshot, Station } from "@/lib/tsunami";

function formatClock(seconds: number): string {
  const s = Math.max(0, Math.round(seconds));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = s % 60;
  return h > 0
    ? `${h}:${String(m).padStart(2, "0")}:${String(ss).padStart(2, "0")}`
    : `${m}:${String(ss).padStart(2, "0")}`;
}

function StationChart({ stations }: { stations: Station[] }) {
  const withData = stations.filter((s) => s.series.length > 1);
  if (withData.length === 0) return null;

  return (
    <div className="h-48 w-full">
      <ResponsiveContainer width="100%" height="100%">
        <LineChart margin={{ top: 4, right: 8, bottom: 0, left: -12 }}>
          <CartesianGrid
            stroke="var(--color-border)"
            strokeDasharray="3 3"
            vertical={false}
          />
          <XAxis
            dataKey="t"
            type="number"
            domain={["dataMin", "dataMax"]}
            tickFormatter={formatClock}
            stroke="var(--color-muted-foreground)"
            fontSize={10}
            tickLine={false}
            axisLine={false}
          />
          <YAxis
            dataKey="eta"
            type="number"
            width={38}
            tickFormatter={(v: number) => v.toFixed(1)}
            stroke="var(--color-muted-foreground)"
            fontSize={10}
            tickLine={false}
            axisLine={false}
          />
          <Tooltip
            contentStyle={{
              background: "var(--color-popover)",
              border: "1px solid var(--color-border)",
              borderRadius: "var(--radius-md)",
              fontSize: 11,
            }}
            labelFormatter={(v) => `T+${formatClock(Number(v))}`}
            formatter={(v) => [`${Number(v).toFixed(2)} m`, "η"]}
          />
          {withData.length > 1 && (
            <Legend wrapperStyle={{ fontSize: 10 }} iconSize={8} />
          )}
          {withData.map((s) => (
            <Line
              key={s.name}
              name={s.name}
              data={s.series}
              dataKey="eta"
              xAxisId={0}
              stroke={stationColor(s.name)}
              strokeWidth={2}
              dot={false}
              isAnimationActive={false}
            />
          ))}
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}

/** Region-view section: place virtual gauges and watch their eta(t) live. */
export function StationsPanel({
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
  const placing = snapshot.placingStation;

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between">
        <h2 className="text-[11px] font-semibold uppercase tracking-[0.15em] text-muted-foreground">
          Messstationen
        </h2>
        {stations.length > 0 && (
          <button
            onClick={clearStations}
            className="text-[10px] text-muted-foreground hover:text-foreground"
          >
            Alle löschen
          </button>
        )}
      </div>

      <Button
        variant={placing ? "default" : "outline"}
        className="w-full"
        onClick={() => togglePlacingStation(!placing)}
      >
        <MapPin /> {placing ? "Auf Karte klicken …" : "Station platzieren"}
      </Button>

      {stations.length === 0 ? (
        <p className="text-xs leading-relaxed text-muted-foreground">
          Platziere virtuelle Pegel im Gelände, um ihre Wasserstands-Zeitreihe
          hier zu verfolgen — auch während die Simulation läuft.
        </p>
      ) : (
        <>
          <div className="space-y-1.5">
            {stations.map((s, i) => {
              const last = s.series.at(-1);
              return (
                <div
                  key={i}
                  className="flex items-center gap-2 rounded-lg border border-border bg-secondary/40 px-2.5 py-1.5"
                >
                  <span
                    className="h-2 w-2 shrink-0 rounded-full"
                    style={{ background: stationColor(s.name) }}
                  />
                  <input
                    value={s.name}
                    onChange={(e) => renameStation(i, e.target.value)}
                    className="min-w-0 flex-1 truncate bg-transparent text-xs outline-none focus:underline"
                  />
                  <span className="shrink-0 font-mono text-[11px] text-muted-foreground">
                    {last ? `${last.eta >= 0 ? "+" : ""}${last.eta.toFixed(2)} m` : "…"}
                  </span>
                  <button
                    onClick={() => removeStation(i)}
                    className="shrink-0 text-muted-foreground hover:text-destructive"
                  >
                    <X className="h-3.5 w-3.5" />
                  </button>
                </div>
              );
            })}
          </div>
          <StationChart stations={stations} />
          {!snapshot.sim.running && (
            <p className="flex items-center gap-1.5 text-[10px] text-muted-foreground">
              <Radio className="h-3 w-3" /> Zeitreihen laufen weiter, sobald
              die Simulation startet.
            </p>
          )}
        </>
      )}
    </div>
  );
}
