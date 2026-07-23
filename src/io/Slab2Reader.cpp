/**
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Global nearest-neighbour lookup into the USGS Slab2 subduction-geometry grids
 * (Hayes et al. 2018), plus a first-run download from ScienceBase.
 **/
#include "Slab2Reader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <netcdf.h>
#include <utility>

namespace {
// Abort on a netCDF error, matching io::NetCDF::checkErr.
void checkErr(int i_err) {
  if (i_err != NC_NOERR) {
    std::cerr << "Slab2Reader netCDF error: " << nc_strerror(i_err)
              << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

// The 27 Slab2 subduction regions on USGS ScienceBase: three-letter code, item
// id, and the remote depth/strike/dip .grd filenames. Files download by name
// via the stable /file/get/<item>?name=<file> endpoint. Local copies drop the
// date so the on-disk names stay stable (data/<code>_slab2_{dep,str,dip}.grd).
struct Slab2Region {
  const char* code;
  const char* item;
  const char* dep;
  const char* str;
  const char* dip;
};
const Slab2Region k_regions[] = {
    {"alu", "5aa2c535e4b0b1c392ea3ca2", "alu_slab2_dep_02.23.18.grd",
     "alu_slab2_str_02.23.18.grd", "alu_slab2_dip_02.23.18.grd"},
    {"cal", "5aa31058e4b0b1c392ea3e63", "cal_slab2_dep_02.24.18.grd",
     "cal_slab2_str_02.24.18.grd", "cal_slab2_dip_02.24.18.grd"},
    {"cam", "5aa31127e4b0b1c392ea3e68", "cam_slab2_dep_02.24.18.grd",
     "cam_slab2_str_02.24.18.grd", "cam_slab2_dip_02.24.18.grd"},
    {"car", "5aa311dbe4b0b1c392ea3ef2", "car_slab2_dep_02.24.18.grd",
     "car_slab2_str_02.24.18.grd", "car_slab2_dip_02.24.18.grd"},
    {"cas", "5aa312cde4b0b1c392ea3ef5", "cas_slab2_dep_02.24.18.grd",
     "cas_slab2_str_02.24.18.grd", "cas_slab2_dip_02.24.18.grd"},
    {"cot", "5aa314dae4b0b1c392ea3efe", "cot_slab2_dep_02.24.18.grd",
     "cot_slab2_str_02.24.18.grd", "cot_slab2_dip_02.24.18.grd"},
    {"hal", "5aa3156fe4b0b1c392ea3f01", "hal_slab2_dep_02.23.18.grd",
     "hal_slab2_str_02.23.18.grd", "hal_slab2_dip_02.23.18.grd"},
    {"hel", "5aa31604e4b0b1c392ea3f04", "hel_slab2_dep_02.24.18.grd",
     "hel_slab2_str_02.24.18.grd", "hel_slab2_dip_02.24.18.grd"},
    {"him", "5aa316fae4b0b1c392ea3f07", "him_slab2_dep_02.24.18.grd",
     "him_slab2_str_02.24.18.grd", "him_slab2_dip_02.24.18.grd"},
    {"hin", "5aa3177ae4b0b1c392ea3f0a", "hin_slab2_dep_02.24.18.grd",
     "hin_slab2_str_02.24.18.grd", "hin_slab2_dip_02.24.18.grd"},
    {"izu", "5aa3185ee4b0b1c392ea3f0d", "izu_slab2_dep_02.24.18.grd",
     "izu_slab2_str_02.24.18.grd", "izu_slab2_dip_02.24.18.grd"},
    {"ker", "5aa318e1e4b0b1c392ea3f10", "ker_slab2_dep_02.24.18.grd",
     "ker_slab2_str_02.24.18.grd", "ker_slab2_dip_02.24.18.grd"},
    {"kur", "5aa4060de4b0b1c392eaaee2", "kur_slab2_dep_02.24.18.grd",
     "kur_slab2_str_02.24.18.grd", "kur_slab2_dip_02.24.18.grd"},
    {"mak", "5aa406f1e4b0b1c392eaaee5", "mak_slab2_dep_02.24.18.grd",
     "mak_slab2_str_02.24.18.grd", "mak_slab2_dip_02.24.18.grd"},
    {"man", "5aa4076fe4b0b1c392eaaee8", "man_slab2_dep_02.24.18.grd",
     "man_slab2_str_02.24.18.grd", "man_slab2_dip_02.24.18.grd"},
    {"mue", "5aa40800e4b0b1c392eaaeeb", "mue_slab2_dep_02.24.18.grd",
     "mue_slab2_str_02.24.18.grd", "mue_slab2_dip_02.24.18.grd"},
    {"pam", "5aa40985e4b0b1c392eaaeee", "pam_slab2_dep_02.26.18.grd",
     "pam_slab2_str_02.26.18.grd", "pam_slab2_dip_02.26.18.grd"},
    {"phi", "5aa40a33e4b0b1c392eaaef4", "phi_slab2_dep_02.26.18.grd",
     "phi_slab2_str_02.26.18.grd", "phi_slab2_dip_02.26.18.grd"},
    {"png", "5aa413f2e4b0b1c392eaaf2a", "png_slab2_dep_02.26.18.grd",
     "png_slab2_str_02.26.18.grd", "png_slab2_dip_02.26.18.grd"},
    {"puy", "5aa412b2e4b0b1c392eaaf27", "puy_slab2_dep_02.26.18.grd",
     "puy_slab2_str_02.26.18.grd", "puy_slab2_dip_02.26.18.grd"},
    {"ryu", "5aa40aafe4b0b1c392eaaefa", "ryu_slab2_dep_02.26.18.grd",
     "ryu_slab2_str_02.26.18.grd", "ryu_slab2_dip_02.26.18.grd"},
    {"sam", "5aa41473e4b0b1c392eaaf2d", "sam_slab2_dep_02.23.18.grd",
     "sam_slab2_str_02.23.18.grd", "sam_slab2_dip_02.23.18.grd"},
    {"sco", "5aa41674e4b0b1c392eaaf31", "sco_slab2_dep_02.23.18.grd",
     "sco_slab2_str_02.23.18.grd", "sco_slab2_dip_02.23.18.grd"},
    {"sol", "5aa41721e4b0b1c392eaaf35", "sol_slab2_dep_02.23.18.grd",
     "sol_slab2_str_02.23.18.grd", "sol_slab2_dip_02.23.18.grd"},
    {"sul", "5aa417cbe4b0b1c392eaaf38", "sul_slab2_dep_02.23.18.grd",
     "sul_slab2_str_02.23.18.grd", "sul_slab2_dip_02.23.18.grd"},
    {"sum", "5aa41834e4b0b1c392eaaf3b", "sum_slab2_dep_02.23.18.grd",
     "sum_slab2_str_02.23.18.grd", "sum_slab2_dip_02.23.18.grd"},
    {"van", "5aa4189ee4b0b1c392eaaf3d", "van_slab2_dep_02.23.18.grd",
     "van_slab2_str_02.23.18.grd", "van_slab2_dip_02.23.18.grd"},
};
const int k_nRegions = (int)(sizeof(k_regions) / sizeof(k_regions[0]));

// Human-readable names for the Slab2 region codes (shown in the UI).
const struct {
  const char* code;
  const char* name;
} k_regionNames[] = {
    {"alu", "Aleuten & Alaska"},
    {"cal", "Kalabrien"},
    {"cam", "Mittelamerika"},
    {"car", "Kleine Antillen"},
    {"cas", "Cascadia"},
    {"cot", "Cotabato"},
    {"hal", "Halmahera"},
    {"hel", "Hellenischer Bogen"},
    {"him", "Himalaya"},
    {"hin", "Hindukusch"},
    {"izu", "Izu-Bonin-Marianen"},
    {"ker", "Tonga-Kermadec"},
    {"kur", "Kurilen-Kamtschatka-Japan"},
    {"mak", "Makran"},
    {"man", "Manila-Graben"},
    {"mue", "Muertos-Graben"},
    {"pam", "Pamir"},
    {"phi", "Philippinen"},
    {"png", "Neuguinea"},
    {"puy", "Puysegur"},
    {"ryu", "Ryukyu-Nankai"},
    {"sam", "Südamerika (Anden)"},
    {"sco", "Scotia (Südsandwich)"},
    {"sol", "Salomonen"},
    {"sul", "Sulawesi"},
    {"sum", "Sumatra-Java"},
    {"van", "Vanuatu"},
};

const char* regionName(const char* i_code) {
  for (const auto& l_n : k_regionNames)
    if (std::string(l_n.code) == i_code)
      return l_n.name;
  return i_code;
}

// Stable on-disk path for a region's quantity grid ("dep" / "str" / "dip").
std::string localPath(const char* i_code, const char* i_quantity) {
  return std::string("data/") + i_code + "_slab2_" + i_quantity + ".grd";
}

// Remote download URL for one of a region's files.
std::string remoteUrl(const Slab2Region& i_reg, const char* i_file) {
  return std::string("https://www.sciencebase.gov/catalog/file/get/") +
         i_reg.item + "?name=" + i_file;
}

// True if the file opens and contains a "z" variable (Slab2's data variable).
bool isValidGrid(const std::string& i_path) {
  int l_ncId = -1;
  if (nc_open(i_path.c_str(), NC_NOWRITE, &l_ncId) != NC_NOERR)
    return false;
  int l_vid = -1;
  bool l_ok = (nc_inq_varid(l_ncId, "z", &l_vid) == NC_NOERR);
  nc_close(l_ncId);
  return l_ok;
}

// True if all three quantity grids of a region are present and readable.
bool regionComplete(const Slab2Region& i_reg) {
  return isValidGrid(localPath(i_reg.code, "dep")) &&
         isValidGrid(localPath(i_reg.code, "str")) &&
         isValidGrid(localPath(i_reg.code, "dip"));
}

// Download i_url to i_path using whichever of curl / wget is available (curl
// ships by default on macOS, wget on many Linux setups). Returns true on
// success.
bool download(const std::string& i_url, const std::string& i_path) {
  if (std::system("command -v curl >/dev/null 2>&1") == 0) {
    std::string l_cmd =
        std::string("curl -fL -o '") + i_path + "' '" + i_url + "'";
    return std::system(l_cmd.c_str()) == 0;
  }
  if (std::system("command -v wget >/dev/null 2>&1") == 0) {
    // -c resumes a partially downloaded file if the run was interrupted.
    std::string l_cmd =
        std::string("wget -c -O '") + i_path + "' '" + i_url + "'";
    return std::system(l_cmd.c_str()) == 0;
  }
  std::fprintf(stderr, "[Slab2] Weder curl noch wget gefunden.\n");
  return false;
}

// Read a single Slab2 .grd's "z" grid as floats, row-major (y, x). The grid is
// uniformly spaced, so its geometry is captured by the origin and step of each
// axis. Accepts both the (y, x) and transposed (x, y) storage orders.
void readGrd(const std::string& i_path,
             size_t& o_nx,
             size_t& o_ny,
             double& o_lonMin,
             double& o_dLon,
             double& o_latMin,
             double& o_dLat,
             std::vector<float>& o_z) {
  int l_ncId = -1;
  checkErr(nc_open(i_path.c_str(), NC_NOWRITE, &l_ncId));

  int l_dimXId = -1, l_dimYId = -1;
  checkErr(nc_inq_dimid(l_ncId, "x", &l_dimXId));
  checkErr(nc_inq_dimid(l_ncId, "y", &l_dimYId));

  size_t l_nx = 0, l_ny = 0;
  checkErr(nc_inq_dimlen(l_ncId, l_dimXId, &l_nx));
  checkErr(nc_inq_dimlen(l_ncId, l_dimYId, &l_ny));
  o_nx = l_nx;
  o_ny = l_ny;

  int l_varX = -1, l_varY = -1;
  checkErr(nc_inq_varid(l_ncId, "x", &l_varX));
  checkErr(nc_inq_varid(l_ncId, "y", &l_varY));

  // Endpoints define the uniform spacing (as in gebco::readRegion).
  auto readCoord = [&](int i_var, size_t i_idx) -> double {
    double l_v = 0.0;
    checkErr(nc_get_var1_double(l_ncId, i_var, &i_idx, &l_v));
    return l_v;
  };
  o_lonMin = readCoord(l_varX, 0);
  o_latMin = readCoord(l_varY, 0);
  o_dLon =
      (l_nx > 1) ? (readCoord(l_varX, l_nx - 1) - o_lonMin) / (l_nx - 1) : 1.0;
  o_dLat =
      (l_ny > 1) ? (readCoord(l_varY, l_ny - 1) - o_latMin) / (l_ny - 1) : 1.0;

  // Data variable "z", read as float (converts from whatever type is stored).
  int l_varZ = -1;
  checkErr(nc_inq_varid(l_ncId, "z", &l_varZ));
  int l_zDims[2] = {-1, -1};
  checkErr(nc_inq_vardimid(l_ncId, l_varZ, l_zDims));

  std::vector<float> l_buf(l_nx * l_ny);
  checkErr(nc_get_var_float(l_ncId, l_varZ, l_buf.data()));

  o_z.assign(l_nx * l_ny, 0.0f);
  if (l_zDims[0] == l_dimYId && l_zDims[1] == l_dimXId) {
    // (y, x) -> already row-major (y, x)
    o_z = l_buf;
  } else if (l_zDims[0] == l_dimXId && l_zDims[1] == l_dimYId) {
    // (x, y) -> transpose to (y, x)
    for (size_t l_ix = 0; l_ix < l_nx; l_ix++)
      for (size_t l_iy = 0; l_iy < l_ny; l_iy++)
        o_z[l_iy * l_nx + l_ix] = l_buf[l_ix * l_ny + l_iy];
  } else {
    std::cerr << "Slab2Reader: '" << i_path
              << "' has z dimensions other than (x, y) or (y, x)" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  checkErr(nc_close(l_ncId));
}
} // namespace

namespace tsunami_lab {
namespace io {

Slab2Reader::Slab2Reader() {
  for (int l_r = 0; l_r < k_nRegions; l_r++) {
    const Slab2Region& l_reg = k_regions[l_r];
    if (!regionComplete(l_reg))
      continue; // region not (fully) downloaded — skip it

    Grid l_g;
    size_t l_nx = 0, l_ny = 0;
    std::vector<float> l_dep, l_str, l_dip;

    readGrd(localPath(l_reg.code, "dep"), l_nx, l_ny, l_g.lonMin, l_g.dLon,
            l_g.latMin, l_g.dLat, l_dep);
    size_t l_snx = 0, l_sny = 0;
    double l_ignore = 0.0;
    readGrd(localPath(l_reg.code, "str"), l_snx, l_sny, l_ignore, l_ignore,
            l_ignore, l_ignore, l_str);
    readGrd(localPath(l_reg.code, "dip"), l_snx, l_sny, l_ignore, l_ignore,
            l_ignore, l_ignore, l_dip);

    if (l_str.size() != l_dep.size() || l_dip.size() != l_dep.size()) {
      std::cerr << "Slab2Reader: region '" << l_reg.code
                << "' has mismatched grid sizes — skipped" << std::endl;
      continue;
    }

    l_g.nx = static_cast<t_idx>(l_nx);
    l_g.ny = static_cast<t_idx>(l_ny);
    l_g.name = regionName(l_reg.code);
    l_g.dep = std::move(l_dep);
    l_g.str = std::move(l_str);
    l_g.dip = std::move(l_dip);

    // Coverage bounds from the axis endpoints (spacing may be signed).
    const double l_lonEnd = l_g.lonMin + (double)(l_nx - 1) * l_g.dLon;
    const double l_latEnd = l_g.latMin + (double)(l_ny - 1) * l_g.dLat;
    l_g.lonLo = std::min(l_g.lonMin, l_lonEnd);
    l_g.lonHi = std::max(l_g.lonMin, l_lonEnd);
    l_g.latLo = std::min(l_g.latMin, l_latEnd);
    l_g.latHi = std::max(l_g.latMin, l_latEnd);

    m_grids.push_back(std::move(l_g));
  }
}

bool Slab2Reader::ensureAvailable() {
  // Count regions already present and usable.
  int l_have = 0;
  for (int l_r = 0; l_r < k_nRegions; l_r++)
    if (regionComplete(k_regions[l_r]))
      l_have++;
  if (l_have == k_nRegions)
    return true;

  std::fprintf(stderr,
               "\n[Slab2] Globale Subduktions-Geometrie unvollständig "
               "(%d/%d Regionen).\n"
               "[Slab2] Einmaliger Download der fehlenden Regionen (~30 MB "
               "gesamt) von USGS ScienceBase …\n\n",
               l_have, k_nRegions);

  if (std::system("mkdir -p data") != 0) {
    std::fprintf(stderr, "[Slab2] Konnte 'data/' nicht anlegen.\n");
    return false;
  }

  for (int l_r = 0; l_r < k_nRegions; l_r++) {
    const Slab2Region& l_reg = k_regions[l_r];
    if (regionComplete(l_reg))
      continue;

    std::fprintf(stderr, "[Slab2] (%d/%d) %s …\n", l_r + 1, k_nRegions,
                 l_reg.code);
    const char* l_files[3] = {l_reg.dep, l_reg.str, l_reg.dip};
    const char* l_quants[3] = {"dep", "str", "dip"};
    for (int l_q = 0; l_q < 3; l_q++) {
      std::string l_local = localPath(l_reg.code, l_quants[l_q]);
      if (isValidGrid(l_local))
        continue;
      if (!download(remoteUrl(l_reg, l_files[l_q]), l_local))
        std::fprintf(stderr, "[Slab2]   Download fehlgeschlagen: %s\n",
                     l_files[l_q]);
    }
  }

  // Recount; a few failed regions are tolerable as long as some are usable.
  l_have = 0;
  for (int l_r = 0; l_r < k_nRegions; l_r++)
    if (regionComplete(k_regions[l_r]))
      l_have++;

  if (l_have == 0) {
    std::fprintf(stderr, "[Slab2] Keine Region verfügbar — Fallback aktiv.\n");
    return false;
  }
  std::fprintf(stderr, "[Slab2] Bereit: %d/%d Regionen.\n\n", l_have,
               k_nRegions);
  return true;
}

} // namespace io
} // namespace tsunami_lab
