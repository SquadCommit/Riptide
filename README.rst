###########
Tsunami Lab
###########

Interactive tsunami simulation in the browser: the finite-volume solver
(FWave) runs as WebAssembly on pthreads, rendering is WebGL2, the UI is
React. Pick a subduction zone on the world map, place an earthquake with
real USGS-Slab2 fault geometry, and watch the wave propagate live over
GEBCO bathymetry.

- Repository: https://github.com/ykoellmann/tsunami_lab
- User documentation: https://ykoellmann.github.io/tsunami_lab/
- Code documentation: https://ykoellmann.github.io/tsunami_lab/doxygen/

.. contents:: What do you want to do?
   :local:
   :depth: 1

**********************
First-time setup
**********************

Everything below assumes these one-time steps are done.

1. Clone the repository (third-party libraries — Catch2, pugixml, glm —
   are downloaded automatically by CMake at configure time)::

       git clone <repo-url>

2. Enable the pre-commit hooks (style check + unit tests)::

       git config core.hooksPath .githooks

3. Install **Docker** (recommended path below) — or the local toolchains
   listed in `Working without Docker`_.

The datasets under ``data/`` (not in the repo, ~7 GB) are downloaded
automatically by the ``data`` step on first run — GEBCO ice-surface
global grid (BODC/CEDA, ~4.3 GB zip) and the USGS Slab2 subduction
grids (ScienceBase, ~140 MB archive). An interrupted download resumes
on the next run. Nothing to do manually; ``tools/fetch_data.py`` also works
standalone.

*******************************
Run the web app (Docker)
*******************************

Four steps, in this order — later steps consume the artifacts of
earlier ones::

    docker compose run --rm web-build   # 1. C++ -> wasm     -> web/public/wasm/
    docker compose run --rm data        # 2. GEBCO/Slab2 (auto-download) -> web/public/data/
    docker compose run --rm frontend    # 3. React build     -> web/dist/
    docker compose up serve             # 4. http://localhost:8080

Step 3 copies ``web/public/`` (wasm + data) into ``web/dist/`` — that is
why it must run last.

**Public hosting:** every push to ``webapp`` deploys the app to GitHub
Pages via ``.github/workflows/deploy.yml`` (one-time repo setup:
*Settings → Pages → Source: GitHub Actions*). Pages cannot send the
COOP/COEP isolation headers the pthreads build needs, so the page
retrofits them at runtime through the vendored
``web/public/coi-serviceworker.min.js`` — expect one automatic reload
on the very first visit.

When something changes, rebuild only what is affected:

===============================  =========================================
You changed …                    Re-run …
===============================  =========================================
C++ (``src/``)                   ``web-build``, then ``frontend``
Scenario list / data tooling     ``data``, then ``frontend``
Frontend (``web/src/``)          ``frontend`` (or use the dev server)
Nothing, just serving            ``docker compose up serve``
===============================  =========================================

*************************************
Develop the frontend (dev server)
*************************************

For UI work you do not need Docker or Emscripten — a prebuilt wasm
bundle is committed under ``web/public/wasm/``::

    cd web
    npm install
    npm run dev          # http://localhost:5173, hot reload, COOP/COEP set

The demo grids under ``web/public/data/`` must exist once (step 2
above, or copy them from a machine that has them).

*****************************
Working without Docker
*****************************

**Wasm build** — needs the Emscripten SDK (verified with 6.0.2)::

    source ~/emsdk/emsdk_env.sh
    emcmake cmake -B build-web && cmake --build build-web -j

**Demo data** — needs Python 3 with ``netCDF4`` and ``numpy``::

    python3 tools/make_web_data.py

**Native solver CLI + unit tests** — needs cmake, a C++11 compiler and
libnetcdf::

    cmake -B build && cmake --build build -j
    ./build/tests            # unit tests
    ./build/tsunami_lab      # batch solver

****************************
Build the documentation
****************************

::

    docker compose run --rm docs        # sphinx -> sphinx/build/html
    # without Docker: pip install sphinx sphinx-rtd-theme
    # then: sphinx-build -b html sphinx/source sphinx/build/html

*********************
Repository layout
*********************

::

    src/
      solvers/, patches/, setups/   numerical core (native + wasm)
      displacement/                 Okada fault model, scaling laws
      io/                           NetCDF, Slab2 reader (query is wasm-safe)
      visualization/                WebGL2 renderer (globe + region views)
      web/                          wasm entry point, embind API, data loaders
    web/                            React + Vite + shadcn/ui frontend
      public/wasm/                  committed wasm bundle (from web-build)
      public/data/                  generated demo grids (gitignored)
    tools/make_web_data.py          GEBCO/Slab2 -> binary bundles
    docker/, docker-compose.yml     reproducible build/dev environments
    sphinx/, docs/                  user documentation, Doxygen config

************
Project team
************

- https://github.com/JanVogt06
- https://github.com/mbbrueckner
- https://github.com/ykoellmann
