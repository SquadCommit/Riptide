import { useEffect, useState } from "react";

// Below this width the two side panels no longer fit next to the 3d view,
// so the UI switches to a single collapsible bottom sheet. Matches Tailwind's
// `lg` breakpoint (1024px).
const MOBILE_MAX_WIDTH = 1023;

/** Tracks whether the viewport is too narrow for the desktop side-panel
 * layout. Updates live on resize / orientation change. */
export function useIsMobile(): boolean {
  const [isMobile, setIsMobile] = useState(
    () =>
      typeof window !== "undefined" &&
      window.matchMedia(`(max-width: ${MOBILE_MAX_WIDTH}px)`).matches,
  );

  useEffect(() => {
    const mq = window.matchMedia(`(max-width: ${MOBILE_MAX_WIDTH}px)`);
    const onChange = () => setIsMobile(mq.matches);
    mq.addEventListener("change", onChange);
    return () => mq.removeEventListener("change", onChange);
  }, []);

  return isMobile;
}
