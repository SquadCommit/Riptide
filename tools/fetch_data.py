#!/usr/bin/env python3
"""Ensures the datasets under data/ exist, downloading anything missing.

  data/GEBCO_2026.nc                     GEBCO ice-surface global grid
                                         (zip ~4.3 GB -> nc ~7.5 GB, BODC/CEDA)
  data/<code>_slab2_{dep,str,dip}.grd    USGS Slab2 subduction grids
                                         (27 regions, from the ~140 MB
                                         distribution tarball on ScienceBase)

Bootstraps the docker data pipeline the way the native CLI's first-run
download does (src/io/Slab2Reader.cpp and, on main, src/visualization/
Gebco.cpp) — but pulls Slab2 from the all-in-one tarball instead of the
per-file endpoint, which 404s for a handful of unpublished files.
Interrupted downloads resume where the server supports range requests.

Requires: pip install netCDF4
"""

import os
import re
import sys
import tarfile
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

# Keep in sync with k_regions in src/io/Slab2Reader.cpp. The tarball holds
# grids named <code>_slab2_<quantity>_<date>.grd; local copies drop the date.
SLAB2_CODES = (
    "alu", "cal", "cam", "car", "cas", "cot", "hal", "hel", "him",
    "hin", "izu", "ker", "kur", "mak", "man", "mue", "pam", "phi",
    "png", "puy", "ryu", "sam", "sco", "sol", "sul", "sum", "van",
)
SLAB2_TAR = os.path.join(DATA, "Slab2Distribute_Mar2018.tar.gz")
SLAB2_URL = ("https://www.sciencebase.gov/catalog/file/get/"
             "5aa1b00ee4b0b1c392e86467?name=Slab2Distribute_Mar2018.tar.gz")


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
    """Extracts any missing/broken Slab2 region grids from the distribution
    tarball, downloading it once on demand. Exits if none end up usable."""
    quantities = ("dep", "str", "dip")

    def complete(code):
        return all(is_valid_nc(os.path.join(DATA, f"{code}_slab2_{q}.grd"),
                               "z") for q in quantities)

    todo = {c for c in SLAB2_CODES if not complete(c)}
    if not todo:
        return
    print(f"[Slab2] {len(todo)}/{len(SLAB2_CODES)} Regionen fehlen — "
          "einmaliger Download des Slab2-Archivs (~140 MB) von USGS "
          "ScienceBase …")
    os.makedirs(DATA, exist_ok=True)
    if not download(SLAB2_URL, SLAB2_TAR, "Slab2Distribute_Mar2018.tar.gz"):
        sys.exit("[Slab2] Download fehlgeschlagen.")

    # Stream just the wanted grids out of the tarball, dropping the date
    # stamp from the name (<code>_slab2_<q>_<date>.grd -> ..._<q>.grd).
    pattern = re.compile(
        r"^(" + "|".join(todo) + r")_slab2_(" + "|".join(quantities)
        + r")_.*\.grd$")
    with tarfile.open(SLAB2_TAR, "r:gz") as tar:
        for member in tar:
            m = pattern.match(os.path.basename(member.name))
            if not m or not member.isfile():
                continue
            local = os.path.join(DATA, f"{m[1]}_slab2_{m[2]}.grd")
            with tar.extractfile(member) as src, open(local, "wb") as dst:
                dst.write(src.read())
            print(f"  {os.path.relpath(local, ROOT)}: "
                  f"{member.size / 1e6:.1f} MB")

    os.remove(SLAB2_TAR)
    have = sum(1 for c in SLAB2_CODES if complete(c))
    if have == 0:
        sys.exit("[Slab2] Keine Region verfügbar.")
    print(f"[Slab2] Bereit: {have}/{len(SLAB2_CODES)} Regionen.")


def main():
    ensure_gebco()
    ensure_slab2()


if __name__ == "__main__":
    main()
