/**
 * @section DESCRIPTION
 * Slab2Reader::query() — the NetCDF-free half of the reader, split out so the
 * web build (which loads pre-converted binary grids instead of .grd files)
 * links the identical lookup logic without pulling in NetCDF.
 **/

#include "Slab2Reader.h"

#include <cmath>
#include <limits>

namespace tsunami_lab {
namespace io {

Slab2Point Slab2Reader::query(double i_lon, double i_lat) const {
  Slab2Point l_best = {0.0, 0.0, 0.0, false, nullptr};
  double l_bestDist = std::numeric_limits<double>::infinity();

  for (const Grid& l_g : m_grids) {
    // Bring the query longitude into this region's range (Slab2 mixes -180..180
    // and 0..360 conventions, and Pacific regions straddle the antimeridian).
    double l_lon = i_lon;
    while (l_lon < l_g.lonLo - 1e-9)
      l_lon += 360.0;
    while (l_lon > l_g.lonHi + 1e-9)
      l_lon -= 360.0;
    if (l_lon < l_g.lonLo || l_lon > l_g.lonHi || i_lat < l_g.latLo ||
        i_lat > l_g.latHi)
      continue;

    // nearest sample on each uniform axis
    long l_ix = std::lround((l_lon - l_g.lonMin) / l_g.dLon);
    long l_iy = std::lround((i_lat - l_g.latMin) / l_g.dLat);
    if (l_ix < 0 || l_ix >= (long)l_g.nx || l_iy < 0 || l_iy >= (long)l_g.ny)
      continue;

    const size_t l_k = (size_t)l_iy * l_g.nx + (size_t)l_ix;
    const float l_z = l_g.dep[l_k];
    const float l_s = l_g.str[l_k];
    const float l_d = l_g.dip[l_k];
    if (std::isnan(l_z) || std::isnan(l_s) || std::isnan(l_d))
      continue; // no slab here in this region

    // Prefer the region whose sampled cell sits closest to the query point.
    const double l_cellLon = l_g.lonMin + (double)l_ix * l_g.dLon;
    const double l_cellLat = l_g.latMin + (double)l_iy * l_g.dLat;
    const double l_dist = (l_cellLon - l_lon) * (l_cellLon - l_lon) +
                          (l_cellLat - i_lat) * (l_cellLat - i_lat);
    if (l_dist < l_bestDist) {
      l_bestDist = l_dist;
      // Slab2 depth "z" is negative-down in km; Okada wants a positive metre
      // depth.
      l_best.depth = -(double)l_z * 1000.0;
      l_best.strike = l_s;
      l_best.dip = l_d;
      l_best.valid = true;
      l_best.region = l_g.name;
    }
  }
  return l_best;
}

} // namespace io
} // namespace tsunami_lab
