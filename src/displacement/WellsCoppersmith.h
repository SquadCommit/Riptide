/**
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Empirical Wells & Coppersmith (1994) scaling relations, deriving a
 * rectangular fault's slip, length and width from moment magnitude.
 **/

#ifndef TSUNAMI_LAB_DISPLACEMENT_WELLSCOPPERSMITH_H
#define TSUNAMI_LAB_DISPLACEMENT_WELLSCOPPERSMITH_H

#include <cmath>

namespace tsunami_lab {
namespace displacement {

class WellsCoppersmith {
public:
  /**
   * Rectangular-fault geometry in SI units.
   **/
  struct FaultGeometry {
    //! dislocation magnitude (metres).
    double slip;
    //! along-strike length (metres).
    double length;
    //! down-dip width (metres).
    double width;
  };

  /**
   * Maps a moment magnitude to a rectangular-fault geometry via the log-linear
   * Wells & Coppersmith scaling laws.
   *
   * @param i_mw moment magnitude.
   * @return fault geometry (slip, length, width) in metres.
   **/
  static FaultGeometry fromMagnitude(double i_mw) {
    FaultGeometry l_geo;
    l_geo.slip = std::pow(10.0, 0.69 * i_mw - 4.80);
    l_geo.length = std::pow(10.0, 0.59 * i_mw - 2.44) * 1000.0;
    l_geo.width = std::pow(10.0, 0.32 * i_mw - 1.01) * 1000.0;
    return l_geo;
  }
};

} // namespace displacement
} // namespace tsunami_lab

#endif
