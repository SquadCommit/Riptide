/**
 * @section DESCRIPTION
 * Empirical fault scaling for subduction-interface earthquakes: rupture
 * length and width from the Strasser, Arango & Bommer (2010) interface
 * regressions, slip from seismic-moment consistency.
 *
 * Wells & Coppersmith (1994) is calibrated on crustal events up to M ~8.1
 * and explicitly excludes subduction-interface earthquakes; extrapolated to
 * M 9+ it yields ruptures that are far too long and narrow (aspect ratio
 * > 10:1). The interface-specific relations reproduce great megathrust
 * sources much better — e.g. Mw 9.1 gives ~700 km x ~205 km with ~10 m of
 * average slip, close to published Tohoku 2011 source models
 * (~450 km x ~200 km, ~10 m).
 **/

#ifndef TSUNAMI_LAB_DISPLACEMENT_SUBDUCTIONSCALING_H
#define TSUNAMI_LAB_DISPLACEMENT_SUBDUCTIONSCALING_H

#include "WellsCoppersmith.h"

#include <cmath>

namespace tsunami_lab {
namespace displacement {

class SubductionScaling {
public:
  /**
   * Maps a moment magnitude to a rectangular interface-fault geometry.
   *
   * Length and width follow Strasser et al. (2010), table 1 (interface):
   *   log10(L) = -2.477 + 0.585 Mw,  log10(W) = -0.882 + 0.351 Mw  (km).
   * The slip is not taken from an independent regression but chosen so the
   * fault carries exactly the seismic moment of the requested magnitude:
   *   D = M0 / (mu L W),  M0 = 10^(1.5 Mw + 9.1) N m.
   *
   * @param i_mw moment magnitude.
   * @param i_mu shear modulus (rigidity) in Pa; 40 GPa is a common value for
   *             the subduction seismogenic zone.
   * @return fault geometry (slip, length, width) in metres.
   **/
  static WellsCoppersmith::FaultGeometry fromMagnitude(double i_mw,
                                                       double i_mu = 40.0e9) {
    WellsCoppersmith::FaultGeometry l_geo;
    l_geo.length = std::pow(10.0, 0.585 * i_mw - 2.477) * 1000.0;
    l_geo.width = std::pow(10.0, 0.351 * i_mw - 0.882) * 1000.0;
    const double l_m0 = std::pow(10.0, 1.5 * i_mw + 9.1);
    l_geo.slip = l_m0 / (i_mu * l_geo.length * l_geo.width);
    return l_geo;
  }
};

} // namespace displacement
} // namespace tsunami_lab

#endif
