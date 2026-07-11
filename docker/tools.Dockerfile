# Python tooling: GEBCO demo-grid extraction (tools/make_web_data.py) and the
# sphinx/doxygen documentation build.
FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        doxygen \
        graphviz \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir \
        netCDF4 \
        numpy \
        sphinx \
        sphinx-rtd-theme

WORKDIR /src
