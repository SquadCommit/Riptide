#include "Slab2Web.h"

#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <vector>

namespace tsunami_lab {
namespace web {

// Grid.name is a raw pointer with expected static storage; the parsed names
// live here for the lifetime of the app (deque: stable addresses).
static std::deque<std::string> s_names;

std::unique_ptr<io::Slab2Reader> parseSlab2(const uint8_t* i_data,
                                            size_t i_size) {
  const uint8_t* l_p = i_data;
  const uint8_t* l_end = i_data + i_size;
  auto need = [&](size_t i_n) { return (size_t)(l_end - l_p) >= i_n; };

  if (!need(8) || std::memcmp(l_p, "TLS2", 4) != 0)
    return nullptr;
  l_p += 4;
  int32_t l_nRegions = 0;
  std::memcpy(&l_nRegions, l_p, 4);
  l_p += 4;
  if (l_nRegions <= 0 || l_nRegions > 64)
    return nullptr;

  const float l_nan = std::numeric_limits<float>::quiet_NaN();
  std::vector<io::Slab2Reader::Grid> l_grids;
  l_grids.reserve(l_nRegions);

  for (int32_t l_r = 0; l_r < l_nRegions; l_r++) {
    if (!need(1))
      return nullptr;
    const uint8_t l_nameLen = *l_p++;
    if (!need((size_t)l_nameLen + 4 * sizeof(double) + 2 * sizeof(int32_t)))
      return nullptr;
    s_names.emplace_back((const char*)l_p, l_nameLen);
    l_p += l_nameLen;

    io::Slab2Reader::Grid l_g;
    double l_hdr[4];
    std::memcpy(l_hdr, l_p, sizeof(l_hdr));
    l_p += sizeof(l_hdr);
    l_g.lonMin = l_hdr[0];
    l_g.latMin = l_hdr[1];
    l_g.dLon = l_hdr[2];
    l_g.dLat = l_hdr[3];

    int32_t l_nx = 0, l_ny = 0;
    std::memcpy(&l_nx, l_p, 4);
    std::memcpy(&l_ny, l_p + 4, 4);
    l_p += 8;
    if (l_nx < 2 || l_ny < 2)
      return nullptr;
    const size_t l_n = (size_t)l_nx * l_ny;
    if (!need(3 * l_n * sizeof(int16_t)))
      return nullptr;

    l_g.nx = (t_idx)l_nx;
    l_g.ny = (t_idx)l_ny;
    l_g.name = s_names.back().c_str();

    // Expand the quantised channels back to the native reader's in-memory
    // convention: depth in km negative-down, strike/dip in degrees, NaN
    // where no slab exists (query() keys off isnan).
    auto expand = [&](std::vector<float>& o_v, double i_scale) {
      o_v.resize(l_n);
      const int16_t* l_src = (const int16_t*)l_p;
      for (size_t l_k = 0; l_k < l_n; l_k++) {
        int16_t l_q;
        std::memcpy(&l_q, &l_src[l_k], sizeof(l_q));
        o_v[l_k] = (l_q == -32768) ? l_nan : (float)(l_q * i_scale);
      }
      l_p += l_n * sizeof(int16_t);
    };
    expand(l_g.dep, -0.1); // 100 m positive-down -> km negative-down
    expand(l_g.str, 0.1);  // 0.1 deg -> deg
    expand(l_g.dip, 0.1);

    const double l_lonEnd = l_g.lonMin + (double)(l_nx - 1) * l_g.dLon;
    const double l_latEnd = l_g.latMin + (double)(l_ny - 1) * l_g.dLat;
    l_g.lonLo = std::min(l_g.lonMin, l_lonEnd);
    l_g.lonHi = std::max(l_g.lonMin, l_lonEnd);
    l_g.latLo = std::min(l_g.latMin, l_latEnd);
    l_g.latHi = std::max(l_g.latMin, l_latEnd);

    l_grids.push_back(std::move(l_g));
  }

  return std::unique_ptr<io::Slab2Reader>(
      new io::Slab2Reader(std::move(l_grids)));
}

} // namespace web
} // namespace tsunami_lab
