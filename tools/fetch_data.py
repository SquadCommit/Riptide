#!/usr/bin/env python3
"""Ensures the datasets under data/ exist, downloading anything missing.

  data/GEBCO_2026.nc                     GEBCO ice-surface global grid
                                         (zip ~4.3 GB -> nc ~7.5 GB, BODC/CEDA)
  data/<code>_slab2_{dep,str,dip}.grd    USGS Slab2 subduction grids
                                         (27 regions, ~30 MB, ScienceBase)

Python port of the first-run download the native CLI performs
(src/io/Slab2Reader.cpp and, on main, src/visualization/Gebco.cpp), so the
docker data pipeline bootstraps itself. Interrupted downloads resume.

Requires: pip install netCDF4
"""

import os
import sys
import urllib.error
import urllib.request
import zipfile

from netCDF4 import Dataset

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")

# The version-stamped filename doubles as the freshness check — bump it to
# GEBCO_2027 etc. to force a re-download.
GEBCO_NC = os.path.join(DATA, "GEBCO_2026.nc")
GEBCO_ZIP = os.path.join(DATA, "GEBCO_2026.zip")
GEBCO_URL = ("https://dap.ceda.ac.uk/bodc/gebco/global/gebco_2026/"
             "ice_surface_elevation/netcdf/GEBCO_2026.zip?download=1")

# Keep in sync with k_regions in src/io/Slab2Reader.cpp: three-letter code ->
# (ScienceBase item id, filename date). Remote names follow the pattern
# <code>_slab2_<quantity>_<date>.grd; local copies drop the date.
SLAB2_REGIONS = {
    "alu": ("5aa2c535e4b0b1c392ea3ca2", "02.23.18"),
    "cal": ("5aa31058e4b0b1c392ea3e63", "02.24.18"),
    "cam": ("5aa31127e4b0b1c392ea3e68", "02.24.18"),
    "car": ("5aa311dbe4b0b1c392ea3ef2", "02.24.18"),
    "cas": ("5aa312cde4b0b1c392ea3ef5", "02.24.18"),
    "cot": ("5aa314dae4b0b1c392ea3efe", "02.24.18"),
    "hal": ("5aa3156fe4b0b1c392ea3f01", "02.23.18"),
    "hel": ("5aa31604e4b0b1c392ea3f04", "02.24.18"),
    "him": ("5aa316fae4b0b1c392ea3f07", "02.24.18"),
    "hin": ("5aa3177ae4b0b1c392ea3f0a", "02.24.18"),
    "izu": ("5aa3185ee4b0b1c392ea3f0d", "02.24.18"),
    "ker": ("5aa318e1e4b0b1c392ea3f10", "02.24.18"),
    "kur": ("5aa4060de4b0b1c392eaaee2", "02.24.18"),
    "mak": ("5aa406f1e4b0b1c392eaaee5", "02.24.18"),
    "man": ("5aa4076fe4b0b1c392eaaee8", "02.24.18"),
    "mue": ("5aa40800e4b0b1c392eaaeeb", "02.24.18"),
    "pam": ("5aa40985e4b0b1c392eaaeee", "02.26.18"),
    "phi": ("5aa40a33e4b0b1c392eaaef4", "02.26.18"),
    "png": ("5aa413f2e4b0b1c392eaaf2a", "02.26.18"),
    "puy": ("5aa412b2e4b0b1c392eaaf27", "02.26.18"),
    "ryu": ("5aa40aafe4b0b1c392eaaefa", "02.26.18"),
    "sam": ("5aa41473e4b0b1c392eaaf2d", "02.23.18"),
    "sco": ("5aa41674e4b0b1c392eaaf31", "02.23.18"),
    "sol": ("5aa41721e4b0b1c392eaaf35", "02.23.18"),
    "sul": ("5aa417cbe4b0b1c392eaaf38", "02.23.18"),
    "sum": ("5aa41834e4b0b1c392eaaf3b", "02.23.18"),
    "van": ("5aa4189ee4b0b1c392eaaf3d", "02.23.18"),
}


def is_valid_nc(path, var):
    """True if the file opens as NetCDF and contains the given variable."""
    if not os.path.exists(path):
        return False
    try:
        ds = Dataset(path, "r")
        ok = var in ds.variables
        ds.close()
        return ok
    except OSError:
        return False


def download(url, path, label):
    """Download url to path via a .part file; resumes an interrupted run
    with an HTTP Range request when the server supports it."""
    part = path + ".part"
    done = os.path.getsize(part) if os.path.exists(part) else 0

    req = urllib.request.Request(url)
    if done:
        req.add_header("Range", f"bytes={done}-")
    try:
        resp = urllib.request.urlopen(req)
    except urllib.error.HTTPError as e:
        if e.code == 416:  # part file already complete
            os.replace(part, path)
            return True
        print(f"  {label}: HTTP {e.code} — {e.reason}", file=sys.stderr)
        return False
    except urllib.error.URLError as e:
        print(f"  {label}: {e.reason}", file=sys.stderr)
        return False

    if done and resp.status != 206:  # no range support: restart from zero
        done = 0
    total = resp.headers.get("Content-Length")
    total = done + int(total) if total else None

    mode = "ab" if done else "wb"
    printed = -1
    with resp, open(part, mode) as f:
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            mb = done >> 27 << 7  # progress line every 128 MB
            if total and total > (1 << 28) and mb > printed:
                printed = mb
                print(f"  {label}: {done / 1e9:.1f} / {total / 1e9:.1f} GB")
    if total and done != total:
        print(f"  {label}: unvollständig ({done}/{total} B) — "
              "erneut ausführen setzt den Download fort.", file=sys.stderr)
        return False
    os.replace(part, path)
    return True


def ensure_gebco():
    """Returns the path of the GEBCO grid, downloading + unzipping first if
    it is missing. Exits on failure (nothing works without bathymetry)."""
    if is_valid_nc(GEBCO_NC, "elevation"):
        return GEBCO_NC

    print(f"[GEBCO] '{os.path.relpath(GEBCO_NC, ROOT)}' nicht gefunden.\n"
          f"[GEBCO] Einmaliger Download (~4.3 GB Zip → ~7.5 GB NetCDF) …")
    os.makedirs(DATA, exist_ok=True)
    if not download(GEBCO_URL, GEBCO_ZIP, "GEBCO_2026.zip"):
        sys.exit("[GEBCO] Download fehlgeschlagen.")

    print(f"[GEBCO] Entpacke {os.path.relpath(GEBCO_ZIP, ROOT)} …")
    with zipfile.ZipFile(GEBCO_ZIP) as z:
        z.extractall(DATA)
    if not is_valid_nc(GEBCO_NC, "elevation"):
        sys.exit(f"[GEBCO] '{GEBCO_NC}' nach dem Entpacken nicht "
                 "vorhanden/lesbar — bitte den Inhalt von data/ prüfen.")

    os.remove(GEBCO_ZIP)  # reclaim the ~4 GB zip; the .nc is all we need
    print(f"[GEBCO] Bereit: {os.path.relpath(GEBCO_NC, ROOT)}")
    return GEBCO_NC


def ensure_slab2():
    """Downloads any missing/broken Slab2 region grids. Failed regions are
    tolerated (they are skipped when bundling), missing all is an error."""
    quantities = ("dep", "str", "dip")

    def complete(code):
        return all(is_valid_nc(os.path.join(DATA, f"{code}_slab2_{q}.grd"),
                               "z") for q in quantities)

    todo = [c for c in SLAB2_REGIONS if not complete(c)]
    if not todo:
        return
    print(f"[Slab2] {len(todo)}/{len(SLAB2_REGIONS)} Regionen fehlen — "
          "Download von USGS ScienceBase (~30 MB gesamt) …")
    os.makedirs(DATA, exist_ok=True)

    for i, code in enumerate(sorted(todo), 1):
        item, date = SLAB2_REGIONS[code]
        print(f"[Slab2] ({i}/{len(todo)}) {code} …")
        for q in quantities:
            local = os.path.join(DATA, f"{code}_slab2_{q}.grd")
            if is_valid_nc(local, "z"):
                continue
            url = (f"https://www.sciencebase.gov/catalog/file/get/{item}"
                   f"?name={code}_slab2_{q}_{date}.grd")
            if not download(url, local, f"{code}_{q}"):
                print(f"[Slab2]   Download fehlgeschlagen: {code}_{q}",
                      file=sys.stderr)

    have = sum(1 for c in SLAB2_REGIONS if complete(c))
    if have == 0:
        sys.exit("[Slab2] Keine Region verfügbar.")
    print(f"[Slab2] Bereit: {have}/{len(SLAB2_REGIONS)} Regionen.")


def main():
    ensure_gebco()
    ensure_slab2()


if __name__ == "__main__":
    main()
