#include "WebData.h"
#include "../visualization/Gebco.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tsunami_lab {
namespace web {

static Grid s_globe;
static Grid s_region;

bool parseGrid(const uint8_t* i_data, size_t i_size, Grid& o_grid) {
  const size_t k_header = 4 + 2 * sizeof(int32_t) + 4 * sizeof(double);
  if (i_size < k_header || std::memcmp(i_data, "TLB1", 4) != 0)
    return false;

  const uint8_t* l_p = i_data + 4;
  int32_t l_w = 0, l_h = 0;
  std::memcpy(&l_w, l_p, 4);
  std::memcpy(&l_h, l_p + 4, 4);
  l_p += 8;
  double l_b[4];
  std::memcpy(l_b, l_p, sizeof(l_b));
  l_p += sizeof(l_b);

  if (l_w < 2 || l_h < 2 ||
      i_size != k_header + (size_t)l_w * l_h * sizeof(int16_t))
    return false;

  o_grid.w = l_w;
  o_grid.h = l_h;
  o_grid.lonMin = l_b[0];
  o_grid.lonMax = l_b[1];
  o_grid.latMin = l_b[2];
  o_grid.latMax = l_b[3];
  o_grid.elev.resize((size_t)l_w * l_h);
  std::memcpy(o_grid.elev.data(), l_p, o_grid.elev.size() * sizeof(int16_t));
  return true;
}

void setGlobeGrid(Grid&& i_grid) { s_globe = std::move(i_grid); }
bool hasGlobeGrid() { return s_globe.valid(); }
void setRegionGrid(Grid&& i_grid) { s_region = std::move(i_grid); }
void clearRegionGrid() { s_region = Grid(); }

// True if i_grid covers i_bbox (with half-a-cell slack at the edges).
static bool covers(const Grid& i_grid, const visualization::BBox& i_bbox) {
  const double l_eLon = 0.5 * i_grid.lonStep();
  const double l_eLat = 0.5 * i_grid.latStep();
  return i_bbox.lonMin >= i_grid.lonMin - l_eLon &&
         i_bbox.lonMax <= i_grid.lonMax + l_eLon &&
         i_bbox.latMin >= i_grid.latMin - l_eLat &&
         i_bbox.latMax <= i_grid.latMax + l_eLat;
}

// The loaded region grid wins when it covers the request (it is always finer
// than the globe grid); otherwise the whole-world grid serves as fallback.
static const Grid& pickSource(const visualization::BBox& i_bbox) {
  if (s_region.valid() && covers(s_region, i_bbox))
    return s_region;
  return s_globe;
}

} // namespace web

// gebco:: interface — same contract as the native NetCDF reader used to
// provide from Gebco.cpp, served from the in-memory grids instead of the
// 7-GB file.

namespace visualization {
namespace gebco {

std::string ensureAvailable() {
  // Data arrives via fetch + web::setGlobeGrid(); nothing to resolve here.
  return web::hasGlobeGrid() ? "web" : "";
}

bool readRegion(const std::string&,
                const BBox& i_bbox,
                Region& o_region,
                int i_maxDim) {
  const web::Grid& l_g = web::pickSource(i_bbox);
  if (!l_g.valid())
    return false;

  auto toIdx = [](double i_v, double i_start, double i_step, int i_n) -> long {
    long l_i = (long)std::floor((i_v - i_start) / i_step + 0.5);
    return std::min(std::max(l_i, 0L), (long)i_n - 1);
  };

  long l_la = toIdx(i_bbox.latMin, l_g.latMin, l_g.latStep(), l_g.h);
  long l_lb = toIdx(i_bbox.latMax, l_g.latMin, l_g.latStep(), l_g.h);
  long l_oa = toIdx(i_bbox.lonMin, l_g.lonMin, l_g.lonStep(), l_g.w);
  long l_ob = toIdx(i_bbox.lonMax, l_g.lonMin, l_g.lonStep(), l_g.w);
  if (l_la > l_lb)
    std::swap(l_la, l_lb);
  if (l_oa > l_ob)
    std::swap(l_oa, l_ob);

  const long l_cntLat = l_lb - l_la + 1;
  const long l_cntLon = l_ob - l_oa + 1;
  if (l_cntLat < 2 || l_cntLon < 2)
    return false;

  long l_stride = 1;
  const long l_big = std::max(l_cntLat, l_cntLon);
  if (i_maxDim > 0 && l_big > i_maxDim)
    l_stride = (l_big + i_maxDim - 1) / i_maxDim;

  const int l_outH = (int)((l_cntLat - 1) / l_stride + 1);
  const int l_outW = (int)((l_cntLon - 1) / l_stride + 1);

  o_region.w = l_outW;
  o_region.h = l_outH;
  o_region.elev.resize((size_t)l_outW * l_outH);
  for (int l_j = 0; l_j < l_outH; l_j++) {
    const size_t l_srcRow = (size_t)(l_la + (long)l_j * l_stride) * l_g.w;
    for (int l_i = 0; l_i < l_outW; l_i++)
      o_region.elev[(size_t)l_j * l_outW + l_i] =
          (float)l_g.elev[l_srcRow + l_oa + (long)l_i * l_stride];
  }

  o_region.latMin = l_g.latMin + (double)l_la * l_g.latStep();
  o_region.latMax =
      l_g.latMin +
      (double)(l_la + (long)(l_outH - 1) * l_stride) * l_g.latStep();
  o_region.lonMin = l_g.lonMin + (double)l_oa * l_g.lonStep();
  o_region.lonMax =
      l_g.lonMin +
      (double)(l_oa + (long)(l_outW - 1) * l_stride) * l_g.lonStep();

  if (l_stride > 1)
    std::printf("[WebData] Auswahl %ld×%ld → %d×%d (Stride %ld).\n", l_cntLon,
                l_cntLat, l_outW, l_outH, l_stride);
  return true;
}

} // namespace gebco
} // namespace visualization
} // namespace tsunami_lab
