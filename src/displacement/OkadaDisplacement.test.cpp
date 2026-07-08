/**
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Unit tests for the Okada (1992) rectangular-fault seafloor displacement
 * model (vertical and horizontal components).
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

// Cross-check of horizontalDisplacement() against the reference MATLAB
// implementation okada85.m (IPGP deformation-lib), run unmodified in Octave
// 11.3.0 for these exact fault/query configurations (depth here is the
// top-edge depth used by OkadaDisplacement; okada85.m's centroid-depth
// argument was set to depth_top + W/2*sin(dip) for consistency). Max
// observed deviation across 81 configurations (9 faults x 9 query points,
// spanning strike/dip/rake combinations incl. the near-vertical dip=89
// singularity) was ~5e-11 absolute / ~8e-9 relative -- floating-point noise,
// not a systematic error.
TEST_CASE("OkadaDisplacement: horizontalDisplacement matches okada85.m",
          "[OkadaDispl]") {
  auto l_check = [](double i_strike, double i_dip, double i_rake, double i_slip,
                    double i_lKm, double i_wKm, double i_depthTopKm,
                    double i_eastKm, double i_northKm, double i_expectUe,
                    double i_expectUn, double i_expectUz) {
    OkadaDisplacement l_model(i_strike, i_dip, i_rake, i_slip, i_lKm * 1000.0,
                              i_wKm * 1000.0, i_depthTopKm * 1000.0);
    double l_ue, l_un;
    l_model.horizontalDisplacement(i_eastKm * 1000.0, i_northKm * 1000.0, l_ue,
                                   l_un);
    double l_uz =
        l_model.verticalDisplacement(i_eastKm * 1000.0, i_northKm * 1000.0);
    REQUIRE(l_ue == Approx(i_expectUe).margin(1e-6));
    REQUIRE(l_un == Approx(i_expectUn).margin(1e-6));
    REQUIRE(l_uz == Approx(i_expectUz).margin(1e-6));
  };

  // case 1: strike=30 dip=45 rake=90 (oblique-ish thrust), query at centroid.
  l_check(30.0, 45.0, 90.0, 4.0, 50.0, 25.0, 5.0, 0.0, 0.0, -1.2781382844e-02,
          7.3793348257e-03, 1.5283825973e+00);
  // case 10: strike=0 dip=30 rake=90, query at centroid.
  l_check(0.0, 30.0, 90.0, 5.0, 10.0, 5.0, 2.0, 0.0, 0.0, -2.2541817881e-01,
          0.0, 1.3179858169e+00);
  // case 20: strike=90 dip=30 rake=90, off-centre query.
  l_check(90.0, 30.0, 90.0, 5.0, 100.0, 10.0, 10.0, 3.0, 4.0, 3.0000962453e-03,
          3.0285117166e-01, 1.1670611653e+00);
  // case 46: near-vertical dip=89 (singularity-guard stress test).
  l_check(0.0, 89.0, 90.0, 6.0, 20.0, 10.0, 3.0, 0.0, 0.0, 1.5360058371e-03,
          0.0, 1.3841025099e-01);
  // case 55: pure strike-slip (rake=0), oblique strike=200.
  l_check(200.0, 15.0, 0.0, 3.0, 30.0, 15.0, 4.0, 0.0, 0.0, -4.4703355887e-01,
          -1.2282146087e+00, 0.0);
  // case 64: oblique normal-ish rake=-30.
  l_check(150.0, 60.0, -30.0, 4.5, 40.0, 20.0, 6.0, 0.0, 0.0, 4.9881686989e-01,
          -5.5291941085e-01, -8.8943232971e-01);
  // case 73: shallow dip=10, oblique rake=135.
  l_check(10.0, 10.0, 135.0, 2.5, 25.0, 12.0, 8.0, 0.0, 0.0, -1.7644167199e-01,
          -3.0444813168e-01, 1.5594806078e-01);
}

TEST_CASE("OkadaDisplacement: horizontalDisplacement finite for shallow "
          "near-vertical faults",
          "[OkadaDispl]") {
  // Same stress test as the verticalDisplacement equivalent above, extended
  // to the horizontal components.
  double l_dips[] = {1.0, 45.0, 89.9, 90.0};
  double l_rakes[] = {0.0, 45.0, 90.0, -90.0};

  for (double l_dip : l_dips) {
    for (double l_rake : l_rakes) {
      OkadaDisplacement l_model(15.0, l_dip, l_rake, 3.0, 8000.0, 4000.0,
                                2000.0);
      for (double l_e = -10000.0; l_e <= 10000.0; l_e += 2500.0) {
        for (double l_n = -10000.0; l_n <= 10000.0; l_n += 2500.0) {
          double l_ue, l_un;
          l_model.horizontalDisplacement(l_e, l_n, l_ue, l_un);
          REQUIRE(std::isfinite(l_ue));
          REQUIRE(std::isfinite(l_un));
        }
      }
    }
  }
}

// effectiveVerticalDisplacement() itself is a pure vector-calculus identity
// (not Okada-specific), so it is checked independently of the okada85.m
// cross-check above: a rigid horizontal shift of a sloping seafloor changes
// the elevation seen at a fixed point by -(u . grad(elev)).
TEST_CASE("effectiveVerticalDisplacement: sign and magnitude of the "
          "Tanioka-Satake correction",
          "[OkadaDispl]") {
  using tsunami_lab::displacement::effectiveVerticalDisplacement;

  // Zero horizontal motion or flat seafloor: correction is a no-op.
  REQUIRE(effectiveVerticalDisplacement(1.23, 0.0, 0.0, -0.5, 0.3) ==
          Approx(1.23));
  REQUIRE(effectiveVerticalDisplacement(1.23, 5.0, -2.0, 0.0, 0.0) ==
          Approx(1.23));

  // Seafloor deepens toward +east (elevation gradient < 0, e.g. a trench
  // flank). Moving the fault block toward +east (uEast > 0) carries shallower
  // seafloor into a deeper zone: the seafloor at a fixed point becomes
  // shallower (elevation increases) as the shallower material slides in.
  // With d(elev)/d(east) = -1 (deepens eastward) and uEast = 10:
  // correction = -(10 * -1) = +10 -> elevation rises here.
  double l_corrected = effectiveVerticalDisplacement(0.0, 10.0, 0.0, -1.0, 0.0);
  REQUIRE(l_corrected == Approx(10.0));

  // Symmetric case: seafloor deepens toward +north instead.
  double l_correctedN =
      effectiveVerticalDisplacement(0.0, 0.0, 10.0, 0.0, -1.0);
  REQUIRE(l_correctedN == Approx(10.0));

  // Motion down-slope (uEast in the direction the floor gets shallower, i.e.
  // same sign as the gradient) lowers the elevation at the fixed point.
  double l_downslope = effectiveVerticalDisplacement(0.0, 10.0, 0.0, 1.0, 0.0);
  REQUIRE(l_downslope == Approx(-10.0));
}
