#ifndef TSUNAMI_LAB_WEB_WEBDATA_H
#define TSUNAMI_LAB_WEB_WEBDATA_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tsunami_lab {
namespace web {

/**
 * In-memory elevation grids backing the gebco::readRegion() interface in the
 * browser build. The native build reads windows out of the 7-GB GEBCO NetCDF
 * on demand; in the browser the JS side fetches pre-extracted binary grids
 * (see tools/make_web_data.py) and hands them over here. readRegion() then
 * serves crops from whichever stored grid covers the request best.
 *
 * Binary grid format (little endian):
 *   char[4]  magic "TLB1"
 *   int32    w, h              samples along lon / lat
 *   float64  lonMin, lonMax, latMin, latMax
 *   int16    elev[w*h]         row-major, row 0 = south, metres
 **/
struct Grid {
  int w = 0, h = 0;
  double lonMin = 0, lonMax = 0, latMin = 0, latMax = 0;
  std::vector<int16_t> elev;

  bool valid() const { return w >= 2 && h >= 2 && !elev.empty(); }
  double lonStep() const { return (lonMax - lonMin) / (w - 1); }
  double latStep() const { return (latMax - latMin) / (h - 1); }
};

// Parses the TLB1 format; returns false on malformed input.
bool parseGrid(const uint8_t* i_data, size_t i_size, Grid& o_grid);

// The whole-world grid fetched once at boot (globe view + coarse fallback
// for selections outside any loaded region grid).
void setGlobeGrid(Grid&& i_grid);
bool hasGlobeGrid();

// The high-resolution grid for the currently loaded region (scenario file or,
// later, stitched tiles). Replaces the previous one.
void setRegionGrid(Grid&& i_grid);
void clearRegionGrid();

} // namespace web
} // namespace tsunami_lab

#endif
