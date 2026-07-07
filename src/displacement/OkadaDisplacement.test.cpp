/**
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Unit tests for the Okada (1992) rectangular-fault seafloor displacement
 * model (vertical component only).
 **/
#include "OkadaDisplacement.h"
#include <catch2/catch.hpp>
#include <cmath>

using tsunami_lab::displacement::OkadaDisplacement;

TEST_CASE("OkadaDisplacement: along-strike symmetry of a thrust fault",
          "[OkadaDispl]") {
  // strike=0 (geographic, clockwise from North) -> along-strike axis is
  // north; a pure dip-slip (thrust) source is symmetric under reflection of
  // the along-strike coordinate.
  OkadaDisplacement l_model(0.0, 30.0, 90.0, 5.0, 10000.0, 5000.0, 2000.0);

  REQUIRE(l_model.verticalDisplacement(1000.0, 3000.0) ==
          Approx(l_model.verticalDisplacement(1000.0, -3000.0)));
  REQUIRE(l_model.verticalDisplacement(-4000.0, 7000.0) ==
          Approx(l_model.verticalDisplacement(-4000.0, -7000.0)));
  REQUIRE(l_model.verticalDisplacement(0.0, 500.0) ==
          Approx(l_model.verticalDisplacement(0.0, -500.0)));
}

TEST_CASE("OkadaDisplacement: strike rotates the fault footprint",
          "[OkadaDispl]") {
  // An elongated fault (L >> W) concentrates its displacement along strike.
  // With strike=0 the rupture runs north: a point far north of the centroid
  // (still over the fault) must see far more displacement than a point the
  // same distance east of it (off the fault's side). Strike=90 (fault runs
  // east) swaps the two.
  const double l_L = 100000.0, l_W = 10000.0;
  OkadaDisplacement l_north(0.0, 30.0, 90.0, 5.0, l_L, l_W, 5000.0);
  OkadaDisplacement l_east(90.0, 30.0, 90.0, 5.0, l_L, l_W, 5000.0);

  const double l_r = 30000.0; // inside L/2, well outside W
  REQUIRE(std::abs(l_north.verticalDisplacement(0.0, l_r)) >
          std::abs(l_north.verticalDisplacement(l_r, 0.0)));
  REQUIRE(std::abs(l_east.verticalDisplacement(l_r, 0.0)) >
          std::abs(l_east.verticalDisplacement(0.0, l_r)));

  // Rotating the query with the strike must reproduce the strike=0 field:
  // both points sit at the same along-strike offset on the fault trace.
  REQUIRE(l_east.verticalDisplacement(l_r, 0.0) ==
          Approx(l_north.verticalDisplacement(0.0, l_r)));
}

TEST_CASE("OkadaDisplacement: negligible displacement in the far field",
          "[OkadaDispl]") {
  OkadaDisplacement l_model(35.0, 20.0, 90.0, 8.0, 12000.0, 6000.0, 3000.0);

  double l_far = 50.0 * l_model.influenceRadius();
  REQUIRE(std::abs(l_model.verticalDisplacement(l_far, 0.0)) < 1e-4);
  REQUIRE(std::abs(l_model.verticalDisplacement(0.0, l_far)) < 1e-4);
  REQUIRE(std::abs(l_model.verticalDisplacement(l_far, l_far)) < 1e-4);
}

TEST_CASE("OkadaDisplacement: thrust fault uplifts the centre",
          "[OkadaDispl]") {
  // rake=90 is pure dip-slip thrust; the surface above the fault rises.
  OkadaDisplacement l_model(0.0, 30.0, 90.0, 5.0, 10000.0, 5000.0, 2000.0);
  REQUIRE(l_model.verticalDisplacement(0.0, 0.0) > 0.0);
}

TEST_CASE("OkadaDisplacement: deep compact fault is approximately Gaussian",
          "[OkadaDispl]") {
  // A small fault buried deep produces a smooth bell-shaped uplift footprint.
  OkadaDisplacement l_model(0.0, 45.0, 90.0, 1.0, 1000.0, 1000.0, 20000.0);

  // Sample along strike (north for strike=0), where the dip-slip field is
  // symmetric and bell-shaped; the cross-strike profile has uplift/subsidence
  // lobes and is not Gaussian-like.
  const double l_sigma = 10000.0;
  double l_peak = l_model.verticalDisplacement(0.0, 0.0);
  double l_atSigma = l_model.verticalDisplacement(0.0, l_sigma);

  REQUIRE(l_peak > 0.0);

  // Peak ratio at one sigma should match a Gaussian (exp(-1/2)) within 20%.
  double l_ratio = l_atSigma / l_peak;
  double l_gauss = std::exp(-0.5);
  REQUIRE(std::abs(l_ratio - l_gauss) / l_gauss < 0.20);

  // Monotone falloff away from the centre.
  REQUIRE(l_model.verticalDisplacement(0.0, 0.0) >
          l_model.verticalDisplacement(0.0, 5000.0));
  REQUIRE(l_model.verticalDisplacement(0.0, 5000.0) >
          l_model.verticalDisplacement(0.0, 15000.0));
}

TEST_CASE("OkadaDisplacement: finite for shallow near-vertical faults",
          "[OkadaDispl]") {
  // Near-vertical dip and a very shallow top edge stress every epsilon guard;
  // no input may yield NaN or Inf.
  double l_dips[] = {1.0, 45.0, 89.9, 90.0};
  double l_rakes[] = {0.0, 45.0, 90.0, -90.0};
  double l_depths[] = {100.0, 2000.0};

  for (double l_dip : l_dips) {
    for (double l_rake : l_rakes) {
      for (double l_depth : l_depths) {
        OkadaDisplacement l_model(15.0, l_dip, l_rake, 3.0, 8000.0, 4000.0,
                                  l_depth);
        for (double l_e = -10000.0; l_e <= 10000.0; l_e += 2500.0) {
          for (double l_n = -10000.0; l_n <= 10000.0; l_n += 2500.0) {
            double l_uz = l_model.verticalDisplacement(l_e, l_n);
            REQUIRE(std::isfinite(l_uz));
          }
        }
      }
    }
  }
}
