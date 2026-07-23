/**
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Global nearest-neighbour lookup into the USGS Slab2 subduction-geometry model
 * (Hayes et al. 2018). Slab2 ships one NetCDF (.grd) grid per quantity — depth,
 * strike and dip — for each of ~27 subduction regions worldwide; this reader
 * loads every region present under data/ and routes a query to whichever region
 * covers it.
 **/

#ifndef TSUNAMI_LAB_IO_SLAB2READER_H
#define TSUNAMI_LAB_IO_SLAB2READER_H

#include "../constants.h"
#include <string>
#include <vector>

namespace tsunami_lab {
namespace io {

/**
 * A single sampled point of the Slab2 subduction-geometry model.
 **/
struct Slab2Point {
  //! depth of the slab interface (metres, positive down).
  double depth;
  //! fault strike (degrees).
  double strike;
  //! fault dip (degrees).
  double dip;
  //! false if no slab covers this location.
  bool valid;
  //! display name of the covering region (static storage); nullptr if none.
  const char* region;
};

class Slab2Reader {
public:
  /**
   * One region's uniformly-spaced grid; values stored row-major (y, x).
   * Public so alternative loaders (the web build parses pre-converted
   * binary grids instead of NetCDF files) can construct a reader.
   **/
  struct Grid {
    //! coordinate of sample (0, 0).
    double lonMin, latMin;
    //! sample spacing (signed).
    double dLon, dLat;
    //! longitude coverage bounds.
    double lonLo, lonHi;
    //! latitude coverage bounds.
    double latLo, latHi;
    //! sample counts.
    t_idx nx, ny;
    //! display name of the region (static storage).
    const char* name;
    //! depth (km, negative down), row-major (y, x).
    std::vector<float> dep;
    //! strike (degrees), row-major (y, x).
    std::vector<float> str;
    //! dip (degrees), row-major (y, x).
    std::vector<float> dip;
  };

  /**
   * Loads every Slab2 region grid present under data/ (see ensureAvailable).
   **/
  Slab2Reader();

  /**
   * Constructs a reader from already-parsed region grids (web build).
   **/
  explicit Slab2Reader(std::vector<Grid>&& i_grids)
      : m_grids(std::move(i_grids)) {}

  /**
   * Samples the slab geometry at a location via nearest neighbour. Where
   * several regions overlap, the closest valid sample wins.
   *
   * @param i_lon longitude in degrees.
   * @param i_lat latitude in degrees.
   * @return the sampled point; valid == false where no slab covers the
   * location.
   **/
  Slab2Point query(double i_lon, double i_lat) const;

  /**
   * Ensures the global Slab2 region grids exist under data/, downloading any
   * missing ones from USGS ScienceBase on first use (analogous to
   * gebco::ensureAvailable). Blocks (and prints progress) while downloading.
   *
   * @return true if at least one complete region is available.
   **/
  static bool ensureAvailable();

private:
  //! loaded region grids, one per available Slab2 region.
  std::vector<Grid> m_grids;
};

} // namespace io
} // namespace tsunami_lab

#endif
