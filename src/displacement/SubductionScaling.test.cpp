/**
 * @section DESCRIPTION
 * Unit tests for the subduction-interface fault scaling (Strasser et al.
 * 2010 length/width, moment-consistent slip).
 **/
#include "SubductionScaling.h"
#include <catch2/catch.hpp>
#include <cmath>

using tsunami_lab::displacement::SubductionScaling;
using tsunami_lab::displacement::WellsCoppersmith;

TEST_CASE("SubductionScaling: Mw 9.1 matches Tohoku-class source dimensions",
          "[SubductionScaling]") {
  // Strasser et al. (2010) interface relations at Mw 9.1; published Tohoku
  // 2011 source models give ~450 km x ~200 km with ~10 m average slip.
  WellsCoppersmith::FaultGeometry l_geo = SubductionScaling::fromMagnitude(9.1);

  REQUIRE(l_geo.length / 1000.0 == Approx(702.3).epsilon(0.01));
  REQUIRE(l_geo.width / 1000.0 == Approx(205.1).epsilon(0.01));
  REQUIRE(l_geo.slip == Approx(9.76).epsilon(0.01));
}

TEST_CASE("SubductionScaling: slip is seismic-moment consistent",
          "[SubductionScaling]") {
  // mu * L * W * D must reproduce M0 = 10^(1.5 Mw + 9.1) exactly.
  const double l_mu = 40.0e9;
  for (double l_mw : {7.0, 8.0, 8.8, 9.1, 9.5}) {
    WellsCoppersmith::FaultGeometry l_geo =
        SubductionScaling::fromMagnitude(l_mw, l_mu);
    const double l_m0 = l_mu * l_geo.length * l_geo.width * l_geo.slip;
    REQUIRE(std::log10(l_m0) == Approx(1.5 * l_mw + 9.1).epsilon(1e-6));
  }
}

TEST_CASE("SubductionScaling: interface faults stay wide at high magnitude",
          "[SubductionScaling]") {
  // The motivation for the interface relations: Wells & Coppersmith (crustal,
  // calibrated to M ~8.1) extrapolates megathrusts far too narrow. At Mw 9+
  // the interface aspect ratio must stay moderate, not degenerate.
  WellsCoppersmith::FaultGeometry l_iface =
      SubductionScaling::fromMagnitude(9.1);
  WellsCoppersmith::FaultGeometry l_crustal =
      WellsCoppersmith::fromMagnitude(9.1);

  REQUIRE(l_iface.length / l_iface.width < 4.0);
  REQUIRE(l_iface.width > 2.0 * l_crustal.width);
}
