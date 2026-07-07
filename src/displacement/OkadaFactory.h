/**
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Assembles an Okada rectangular-fault model from a moment magnitude and a
 * geographic location: the fault geometry comes from subduction-interface
 * scaling (Strasser et al. 2010 + moment-consistent slip), the local
 * depth/strike/dip from the Slab2 subduction model.
 **/

#ifndef TSUNAMI_LAB_DISPLACEMENT_OKADAFACTORY_H
#define TSUNAMI_LAB_DISPLACEMENT_OKADAFACTORY_H

#include "OkadaDisplacement.h"
#include "SubductionScaling.h"
#include "io/Slab2Reader.h"
#include <memory>

namespace tsunami_lab {
namespace displacement {

class OkadaFactory {
public:
  /**
   * Builds an Okada displacement for an earthquake of the given magnitude at
   * the given location. The Slab2 model supplies the local fault depth,
   * strike and dip. Since a valid Slab2 sample implies a subduction
   * interface, slip, length and width come from the interface-specific
   * scaling of SubductionScaling, not from Wells & Coppersmith.
   *
   * @param i_mw    moment magnitude.
   * @param i_lon   epicentre longitude in degrees.
   * @param i_lat   epicentre latitude in degrees.
   * @param i_slab2 Slab2 reader used to query the subduction geometry.
   * @param i_rake  fault rake in degrees (90 = pure thrust).
   * @param i_nu    Poisson's ratio.
   * @return the Okada model, or nullptr if the location is outside Slab2
   *         coverage.
   **/
  static std::unique_ptr<OkadaDisplacement>
  fromMagnitudeAndLocation(double i_mw,
                           double i_lon,
                           double i_lat,
                           io::Slab2Reader& i_slab2,
                           double i_rake = 90.0,
                           double i_nu = 0.25) {
    io::Slab2Point l_pt = i_slab2.query(i_lon, i_lat);
    if (!l_pt.valid)
      return nullptr;

    WellsCoppersmith::FaultGeometry l_geo =
        SubductionScaling::fromMagnitude(i_mw);

    return std::unique_ptr<OkadaDisplacement>(
        new OkadaDisplacement(l_pt.strike, l_pt.dip, i_rake, l_geo.slip,
                              l_geo.length, l_geo.width, l_pt.depth, i_nu));
  }
};

} // namespace displacement
} // namespace tsunami_lab

#endif
