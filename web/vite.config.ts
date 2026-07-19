import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import { defineConfig } from "vite";
import { fileURLToPath } from "node:url";

// The wasm build uses pthreads (SharedArrayBuffer), which browsers only
// enable on cross-origin-isolated pages — dev server and preview must send
// COOP/COEP, and so must production hosting.
const isolationHeaders = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
};

export default defineConfig({
  // Relative asset URLs so the build also works from a sub-path
  // (GitHub Pages serves at /<repo>/).
  base: "./",
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: { "@": fileURLToPath(new URL("./src", import.meta.url)) },
  },
  server: { headers: isolationHeaders, port: 5173 },
  preview: { headers: isolationHeaders },
  build: { target: "esnext", chunkSizeWarningLimit: 1024 },
});
