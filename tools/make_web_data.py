#!/usr/bin/env python3
"""Extracts the static demo grids for the web build from the local GEBCO file.

Writes web/public/data/ (gitignored):
  globe.bin        whole world, ~4320 samples wide  (globe view + coarse
                   fallback for free selections)
  scenario_<i>.bin one high-res grid per historical scenario (<=1200/axis)

Binary format "TLB1" (little endian), see src/web/WebData.h:
  char[4] magic | int32 w, h | float64 lonMin, lonMax, latMin, latMax
  | int16 elev[w*h] row-major, row 0 = south

Requires: pip install netCDF4 numpy
"""

import os
import struct
import sys

import numpy as np

try:
    from netCDF4 import Dataset
except ImportError:
    sys.exit("netCDF4 fehlt:  pip3 install netCDF4")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEBCO = os.path.join(ROOT, "data", "GEBCO_2026.nc")
OUT = os.path.join(ROOT, "web", "public", "data")

GLOBE_LON_SAMPLES = 4320  # ~0.083 deg; ~18 MB raw, fine as a one-time fetch
REGION_MAX_DIM = 1200     # matches the sim-setup crop in AppWeb.cpp

# Keep in sync with src/visualization/Scenario.h (k_scenarios).
SCENARIOS = [
    ("Tohoku 2011",          138.0, 148.0, 34.0, 42.0),
    ("Sumatra-Andaman 2004",  90.0, 100.0, -2.0, 10.0),
    ("Chile/Valdivia 1960",  -78.0, -68.0, -45.0, -34.0),
    ("Chile/Maule 2010",     -76.0, -70.0, -38.0, -32.0),
    ("Alaska 1964",         -152.0, -140.0, 58.0, 62.0),
]


def write_grid(path, elev, lon, lat):
    """elev: 2D int16 array (rows = lat), lon/lat: 1D coordinate arrays."""
    if lat[0] > lat[-1]:  # ensure row 0 = south
        lat = lat[::-1]
        elev = elev[::-1, :]
    h, w = elev.shape
    with open(path, "wb") as f:
        f.write(b"TLB1")
        f.write(struct.pack("<ii", w, h))
        f.write(struct.pack("<dddd", float(lon[0]), float(lon[-1]),
                            float(lat[0]), float(lat[-1])))
        f.write(np.ascontiguousarray(elev, dtype="<i2").tobytes())
    print(f"  {os.path.relpath(path, ROOT)}: {w}x{h}, "
          f"{os.path.getsize(path) / 1e6:.1f} MB")


def extract(nc, lon_min, lon_max, lat_min, lat_max, max_dim):
    lon = nc.variables["lon"][:]
    lat = nc.variables["lat"][:]
    i0, i1 = np.searchsorted(lon, [lon_min, lon_max])
    j0, j1 = np.searchsorted(lat, [lat_min, lat_max])
    i1 = min(i1 + 1, len(lon))
    j1 = min(j1 + 1, len(lat))
    stride = max(1, -(-max(i1 - i0, j1 - j0) // max_dim))  # ceil div
    elev = nc.variables["elevation"][j0:j1:stride, i0:i1:stride]
    elev = np.clip(np.asarray(elev, dtype=np.float32), -32768, 32767)
    return elev.astype(np.int16), lon[i0:i1:stride], lat[j0:j1:stride]


def main():
    if not os.path.exists(GEBCO):
        sys.exit(f"GEBCO-Datei nicht gefunden: {GEBCO}")
    os.makedirs(OUT, exist_ok=True)
    nc = Dataset(GEBCO, "r")

    print("Welt-Gitter ...")
    elev, lon, lat = extract(nc, -180.0, 180.0, -90.0, 90.0,
                             GLOBE_LON_SAMPLES)
    write_grid(os.path.join(OUT, "globe.bin"), elev, lon, lat)

    for i, (name, lon0, lon1, lat0, lat1) in enumerate(SCENARIOS):
        print(f"Szenario {i} ({name}) ...")
        elev, lon, lat = extract(nc, lon0, lon1, lat0, lat1, REGION_MAX_DIM)
        write_grid(os.path.join(OUT, f"scenario_{i}.bin"), elev, lon, lat)

    nc.close()
    print("Fertig.")


if __name__ == "__main__":
    main()
