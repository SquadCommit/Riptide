# Native toolchain: solver CLI + unit tests (and the pre-commit checks).
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        libnetcdf-dev \
        clang-format \
        cppcheck \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
