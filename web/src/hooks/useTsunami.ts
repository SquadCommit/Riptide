import { useCallback, useEffect, useRef, useState } from "react";

import {
  fetchBytes,
  withHeapBytes,
  type AppSnapshot,
  type Scenario,
  type TsunamiModule,
} from "@/lib/tsunami";

// Resolves the data grids relative to wherever the app is served from
// ("/" in dev, "./" in the build — GitHub Pages hosts at /<repo>/).
const DATA_BASE = import.meta.env.BASE_URL + "data/";

// React StrictMode double-invokes effects in dev; the emscripten module
// (and its pthread pool) must only ever be created once.
let modulePromise: Promise<TsunamiModule> | null = null;
function getModule(): Promise<TsunamiModule> {
  modulePromise ??= window.createTsunamiModule();
  return modulePromise;
}

export type BootPhase =
  | { phase: "loading"; detail: string }
  | { phase: "error"; detail: string }
  | { phase: "ready" };

/**
 * Owns the wasm module lifecycle: boot sequence (module + globe grid +
 * Slab2 bundle), the render loop, resize plumbing and a polled state
 * snapshot for the UI. The returned `mod` is stable once ready.
 */
export function useTsunami(viewRef: React.RefObject<HTMLDivElement | null>) {
  const modRef = useRef<TsunamiModule | null>(null);
  const [boot, setBoot] = useState<BootPhase>({
    phase: "loading",
    detail: "Modul wird geladen …",
  });
  const [snapshot, setSnapshot] = useState<AppSnapshot | null>(null);
  const [scenarios, setScenarios] = useState<Scenario[]>([]);
  const [scenarioLoading, setScenarioLoading] = useState<number | null>(null);

  useEffect(() => {
    let cancelled = false;

    (async () => {
      try {
        const mod = await getModule();
        if (cancelled) return;
        setBoot({ phase: "loading", detail: "Welt-Bathymetrie (GEBCO) …" });
        const globe = await fetchBytes(DATA_BASE + "globe.bin.gz");
        if (cancelled) return;

        setBoot({ phase: "loading", detail: "Renderer startet …" });
        const rect = viewRef.current!.getBoundingClientRect();
        const ok = withHeapBytes(mod, globe, (p, n) =>
          mod.boot(
            p,
            n,
            Math.max(1, Math.round(rect.width)),
            Math.max(1, Math.round(rect.height)),
            window.devicePixelRatio,
          ),
        );
        if (!ok) throw new Error("WebGL2-Start fehlgeschlagen");
        modRef.current = mod;
        // Debug handle for the console/e2e tests.
        (window as unknown as Record<string, unknown>).__tsunami = mod;
        setScenarios(mod.getScenarios());
        setBoot({ phase: "ready" });

        // Subduction zones stream in after boot — the app is usable
        // without them; overlay + real fault geometry appear once parsed.
        fetchBytes(DATA_BASE + "slab2.bin.gz")
          .then((b) =>
            withHeapBytes(mod, b, (p, n) => mod.loadSlab2Bytes(p, n)),
          )
          .catch((e) => console.warn("Slab2 nicht geladen:", e));
      } catch (e) {
        console.error(e);
        if (!cancelled) setBoot({ phase: "error", detail: String(e) });
      }
    })();

    return () => {
      cancelled = true;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Render loop: rAF plus an interval fallback — rAF pauses in hidden tabs,
  // but renderFrame also polls the displacement worker and the running sim.
  useEffect(() => {
    if (boot.phase !== "ready") return;
    let raf = 0;
    let lastRaf = 0;
    const tick = () => {
      lastRaf = performance.now();
      modRef.current?.renderFrame();
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    const fallback = window.setInterval(() => {
      if (performance.now() - lastRaf > 500) modRef.current?.renderFrame();
    }, 250);
    return () => {
      cancelAnimationFrame(raf);
      window.clearInterval(fallback);
    };
  }, [boot.phase]);

  // UI snapshot polling.
  useEffect(() => {
    if (boot.phase !== "ready") return;
    const id = window.setInterval(() => {
      const mod = modRef.current;
      if (mod) setSnapshot(mod.getState());
    }, 100);
    return () => window.clearInterval(id);
  }, [boot.phase]);

  // Canvas resize.
  useEffect(() => {
    if (boot.phase !== "ready" || !viewRef.current) return;
    const el = viewRef.current;
    const ro = new ResizeObserver(() => {
      const r = el.getBoundingClientRect();
      modRef.current?.resize(
        Math.max(1, Math.round(r.width)),
        Math.max(1, Math.round(r.height)),
        window.devicePixelRatio,
      );
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, [boot.phase, viewRef]);

  const loadScenario = useCallback(async (idx: number) => {
    const mod = modRef.current;
    if (!mod) return;
    setScenarioLoading(idx);
    try {
      const bytes = await fetchBytes(`${DATA_BASE}scenario_${idx}.bin.gz`);
      withHeapBytes(mod, bytes, (p, n) => mod.loadScenarioBytes(idx, p, n));
    } catch (e) {
      console.error(e);
    } finally {
      setScenarioLoading(null);
    }
  }, []);

  return {
    mod: modRef,
    boot,
    snapshot,
    scenarios,
    scenarioLoading,
    loadScenario,
  };
}
