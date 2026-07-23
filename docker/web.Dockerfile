# WebAssembly toolchain for the browser build (tsunami_web).
# Pinned to the emsdk version the port was verified with.
FROM emscripten/emsdk:6.0.2

RUN apt-get update && apt-get install -y --no-install-recommends \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
