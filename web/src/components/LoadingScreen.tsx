import { Waves } from "lucide-react";

import type { BootPhase } from "@/hooks/useTsunami";

export function LoadingScreen({ boot }: { boot: BootPhase }) {
  if (boot.phase === "ready") return null;
  return (
    <div className="absolute inset-0 z-50 flex flex-col items-center justify-center gap-6 bg-background">
      <div className="flex items-center gap-3">
        <Waves className="h-10 w-10 text-primary" />
        <div className="text-3xl font-semibold tracking-[0.25em]">
          TSUNAMI<span className="text-primary">LAB</span>
        </div>
      </div>
      {boot.phase === "loading" ? (
        <div className="flex flex-col items-center gap-3">
          <div className="h-1 w-56 overflow-hidden rounded-full bg-secondary">
            <div className="h-full w-1/3 animate-[loading-slide_1.2s_ease-in-out_infinite] rounded-full bg-primary" />
          </div>
          <div className="text-sm text-muted-foreground">{boot.detail}</div>
        </div>
      ) : (
        <div className="max-w-md text-center text-sm text-red-400">
          {boot.detail}
        </div>
      )}
      <style>{`@keyframes loading-slide {
        0% { transform: translateX(-120%); }
        100% { transform: translateX(280%); }
      }`}</style>
    </div>
  );
}
