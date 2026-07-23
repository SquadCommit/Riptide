#!/usr/bin/env python3
"""Dev server for the web build.

Plain `python3 -m http.server` does not work: the wasm build uses pthreads
(SharedArrayBuffer), which browsers only enable on cross-origin-isolated
pages — the COOP/COEP headers below provide that. Production hosting must
send the same two headers.

Serves the production build (web/dist, created by `npm run build`);
during development use `npm run dev` instead (vite sends the same headers).

Usage:  python3 web/serve.py [port]   (default 8080), then open
        http://localhost:8080/
"""

import http.server
import os
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "dist"))
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"http://localhost:{port}/")
    http.server.ThreadingHTTPServer(("", port), Handler).serve_forever()
