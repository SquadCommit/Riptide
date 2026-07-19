#!/usr/bin/env python3
"""Extracts the static demo grids for the web build from the local GEBCO file.
Missing datasets (GEBCO, Slab2) are downloaded first via fetch_data.py.

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
REGION_MAX_DIM = 2500     # native 15-arcsec for 10-deg scenarios; sim crops to <=2048

# Keep in sync with src/visualization/Scenario.h (k_scenarios).
SCENARIOS = [
    ("Tohoku 2011",          138.0, 148.0, 34.0, 42.0),
    ("Sumatra-Andaman 2004",  90.0, 100.0, -2.0, 10.0),
    ("Chile/Valdivia 1960",  -78.0, -68.0, -45.0, -34.0),
    ("Chile/Maule 2010",     -76.0, -70.0, -38.0, -32.0),
    ("Alaska 1964",         -152.0, -140.0, 58.0, 62.0),
]


def write_grid(path, elev, lon, lat):
    """elev: 2D int16 array (rows = lat), lon/lat: 1D coordinate arrays.
    Written gzip-compressed (.gz suffix); the frontend inflates via
    DecompressionStream."""
    import gzip
    if lat[0] > lat[-1]:  # ensure row 0 = south
        lat = lat[::-1]
        elev = elev[::-1, :]
    h, w = elev.shape
    with gzip.open(path, "wb", compresslevel=6) as f:
        f.write(b"TLB1")
        f.write(struct.pack("<ii", w, h))
        f.write(struct.pack("<dddd", float(lon[0]), float(lon[-1]),
                            float(lat[0]), float(lat[-1])))
        f.write(np.ascontiguousarray(elev, dtype="<i2").tobytes())
    print(f"  {os.path.relpath(path, ROOT)}: {w}x{h}, "
          f"{os.path.getsize(path) / 1e6:.1f} MB gz")


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


# Keep in sync with k_regionNames in src/io/Slab2Reader.cpp.
SLAB2_REGIONS = {
    "alu": "Aleuten & Alaska", "cal": "Kalabrien", "cam": "Mittelamerika",
    "car": "Kleine Antillen", "cas": "Cascadia", "cot": "Cotabato",
    "hal": "Halmahera", "hel": "Hellenischer Bogen", "him": "Himalaya",
    "hin": "Hindukusch", "izu": "Izu-Bonin-Marianen", "ker": "Tonga-Kermadec",
    "kur": "Kurilen-Kamtschatka-Japan", "mak": "Makran",
    "man": "Manila-Graben", "mue": "Muertos-Graben", "pam": "Pamir",
    "phi": "Philippinen", "png": "Neuguinea", "puy": "Puysegur",
    "ryu": "Ryukyu-Nankai", "sam": "Südamerika (Anden)",
    "sco": "Scotia (Südsandwich)", "sol": "Salomonen", "sul": "Sulawesi",
    "sum": "Sumatra-Java", "van": "Vanuatu",
}


def read_grd(path):
    """GMT .grd: axes x/y (or lon/lat), values in variable 'z'."""
    ds = Dataset(path, "r")
    v = ds.variables
    x = v["x"][:] if "x" in v else v["lon"][:]
    y = v["y"][:] if "y" in v else v["lat"][:]
    z = np.ma.filled(v["z"][:].astype(np.float64), np.nan)
    ds.close()
    return np.asarray(x, dtype=np.float64), np.asarray(y, np.float64), z


def quantise(a, scale):
    """float → int16 in 1/scale units; NaN → -32768 (nodata)."""
    q = np.full(a.shape, -32768, dtype=np.int16)
    m = ~np.isnan(a)
    q[m] = np.clip(np.round(a[m] * scale), -32767, 32767).astype(np.int16)
    return q


def write_slab2():
    """Bundles all local Slab2 region grids into slab2.bin.gz (TLS2 format,
    see src/web/Slab2Web.h)."""
    import gzip
    import io as _io

    buf = _io.BytesIO()
    regions = []
    for code, name in sorted(SLAB2_REGIONS.items()):
        paths = {q: os.path.join(ROOT, "data", f"{code}_slab2_{q}.grd")
                 for q in ("dep", "str", "dip")}
        if not all(os.path.exists(p) for p in paths.values()):
            print(f"  {code}: Grids fehlen — übersprungen")
            continue
        x, y, dep = read_grd(paths["dep"])
        _, _, s = read_grd(paths["str"])
        _, _, d = read_grd(paths["dip"])
        if s.shape != dep.shape or d.shape != dep.shape:
            print(f"  {code}: Grid-Größen passen nicht — übersprungen")
            continue
        ny, nx = dep.shape
        nb = name.encode("utf-8")
        buf.write(struct.pack("<B", len(nb)))
        buf.write(nb)
        buf.write(struct.pack("<dddd", float(x[0]), float(y[0]),
                              float(x[1] - x[0]), float(y[1] - y[0])))
        buf.write(struct.pack("<ii", nx, ny))
        # dep: .grd is km negative-down → store 100-m units positive-down
        buf.write(quantise(-dep * 10.0, 1.0).tobytes())
        buf.write(quantise(s, 10.0).tobytes())
        buf.write(quantise(d, 10.0).tobytes())
        regions.append(code)

    out = os.path.join(OUT, "slab2.bin.gz")
    payload = b"TLS2" + struct.pack("<i", len(regions)) + buf.getvalue()
    with gzip.open(out, "wb", compresslevel=9) as f:
        f.write(payload)
    print(f"  {os.path.relpath(out, ROOT)}: {len(regions)} Regionen, "
          f"{len(payload) / 1e6:.1f} MB roh → "
          f"{os.path.getsize(out) / 1e6:.1f} MB gz")


def main():
    import fetch_data
    fetch_data.ensure_gebco()
    fetch_data.ensure_slab2()
    os.makedirs(OUT, exist_ok=True)
    nc = Dataset(GEBCO, "r")

    print("Welt-Gitter ...")
    elev, lon, lat = extract(nc, -180.0, 180.0, -90.0, 90.0,
                             GLOBE_LON_SAMPLES)
    write_grid(os.path.join(OUT, "globe.bin.gz"), elev, lon, lat)

    for i, (name, lon0, lon1, lat0, lat1) in enumerate(SCENARIOS):
        print(f"Szenario {i} ({name}) ...")
        elev, lon, lat = extract(nc, lon0, lon1, lat0, lat1, REGION_MAX_DIM)
        write_grid(os.path.join(OUT, f"scenario_{i}.bin.gz"), elev, lon, lat)

    nc.close()

    print("Slab2-Subduktionszonen ...")
    write_slab2()
    print("Fertig.")


if __name__ == "__main__":
    main()
