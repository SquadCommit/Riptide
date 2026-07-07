/**
 * @author Yannik Köllmann (yannik.koellmann AT uni-jena.de)
 * @author Jan Vogt (jan.vogt AT uni-jena.de)
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * F-wave solver for the shallow water equations.
 **/

#include <catch2/catch.hpp>
#define private public
#include "FWave.h"
#undef public

TEST_CASE("Test the derivation of the FWave speeds.", "[FWaveSpeeds]") {
  /*
   * Test case:
   *  h: 10 | 9
   *  u: -3 | 3
   *
   * roe height: 9.5
   * roe velocity: (sqrt(10) * -3 + 3 * 3) / ( sqrt(10) + sqrt(9) )
   *               = -0.0790021169691720
   * roe speeds: s1 = -0.079002116969172024 - sqrt(9.80665 * 9.5) =
   * -9.7311093998375095 s2 = -0.079002116969172024 + sqrt(9.80665 * 9.5)
   * =  9.5731051658991654
   */
  float l_waveSpeedL = 0;
  float l_waveSpeedR = 0;
  tsunami_lab::solvers::FWave::waveSpeeds(10, 9, -3, 3, l_waveSpeedL,
                                          l_waveSpeedR);

  REQUIRE(l_waveSpeedL == Approx(-9.7311093998375095));
  REQUIRE(l_waveSpeedR == Approx(9.5731051658991654));
}

TEST_CASE("Test the derivation of the FWave flux.", "[FWaveFlux]") {
  /* Test case:
   *  h = 10
   *  hu = -30
   *
   *  expected: f[0] = -30
   *            f[1] =  (-30)^2 / 10 + 0.5 * 9.80665  * 10^2  = 580.3325
   */

  float l_flux[2] = {0};
  tsunami_lab::solvers::FWave::flux(10, -30, l_flux);

  REQUIRE(l_flux[0] == Approx(-30.0f));
  REQUIRE(l_flux[1] == Approx(580.3325f));

  /* Test case:
   *  h = 9
   *  hu = 27
   *
   *  expected: f[0] = 27
   *            f[1] =  (27)^2 / 9 + 0.5 * 9.80665  * 9^2  = 478.169325
   */

  tsunami_lab::solvers::FWave::flux(9, 27, l_flux);

  REQUIRE(l_flux[0] == Approx(27.0f));
  REQUIRE(l_flux[1] == Approx(478.169325f));
}

TEST_CASE("Test the derivation of the FWave wave strength.",
          "[FWaveStrengths]") {
  /*
   * Test case:
   *  h:   10 | 9
   *  u:   -3 | 3
   *  hu: -30 | 27
   *
   * The derivation of the FWave speeds (s1, s2) and fluxes (f_l, f_r) is given
   * above.
   *
   *  Matrix of right eigenvectors:
   *
   *      | 1   1 |
   *  R = |       |
   *      | s1 s2 |
   *
   * Inversion yields:
   *
   * wolframalpha.com query: invert {{1, 1},
   * {-9.7311093998375095, 9.5731051658991654}}
   *
   *        | 0.49590751974393229 -0.051802159398648326 |
   * Rinv = |                                           |
   *        | 0.50409248025606771  0.051802159398648326 |
   *
   * Multiplication with the jump in fluxes gives the wave strengths:
   *
   * wolframalpha.com query: {{0.49590751974393229, -0.051802159398648326},
   * {0.50409248025606771, 0.051802159398648326}} * {27--30,
   * 478.169325-580.3325}
   *
   *        | 27 - -30              |   | 33.55900170142614  |
   * Rinv * |                       | = |                    |
   *        | 478.169325 - 580.3325 |   | 23.440998298573856 |
   */
  float l_strengthL = 0;
  float l_strengthR = 0;

  // flat bathymetry (b_L = b_R = 0) → deltaXPsi vanishes
  tsunami_lab::solvers::FWave::waveStrengths(
      10, 9, -30, 27, 0, 0, -9.7311093998375095, 9.5731051658991654,
      l_strengthL, l_strengthR);

  REQUIRE(l_strengthL == Approx(33.55900170142614));
  REQUIRE(l_strengthR == Approx(23.440998298573856));
}

TEST_CASE("Test the derivation of the FWave net-updates.", "[FWaveUpdates]") {
  /*
   * Test case:
   *
   *      left | right
   *  h:    10 | 9
   *  u:    -3 | 3
   *  hu:  -30 | 27
   *
   * The derivation of the FWave speeds (s1, s2) and wave strengths (a1, a1) is
   * given above.
   *
   * The net-updates are given through the scaled eigenvectors.
   *
   *                      |  1 |   | 33.55900170142614          |
   * update #1:      a1 * |    | = |                            |
   *                      | s1 |   | -326.56631690591088539375  |
   *
   *                      |  1 |   | 23.440998298573856         |
   * update #2:      a2 * |    | = |                            |
   *                      | s2 |   | 224.40314190591092761910   |
   */

  float l_netUpdatesL[2] = {-5, 3};
  float l_netUpdatesR[2] = {4, 7};

  // flat bathymetry (b_L = b_R = 0) → deltaXPsi vanishes; tests stay identical
  tsunami_lab::solvers::FWave::netUpdates(10, 9, -30, 27, 0, 0, l_netUpdatesL,
                                          l_netUpdatesR);

  REQUIRE(l_netUpdatesL[0] == Approx(33.55900170142614));
  REQUIRE(l_netUpdatesL[1] == Approx(-326.56631690591088));

  REQUIRE(l_netUpdatesR[0] == Approx(23.440998298573856));
  REQUIRE(l_netUpdatesR[1] == Approx(224.40314190591092));

  /*
   * Test case (dam break):
   *
   *     left | right
   *   h:  10 | 8
   *   hu:  0 | 0
   *
   * FWave speeds are given as:
   *
   *   s1 = -sqrt(9.80665 * 9) = -9.39468...
   *   s2 =  sqrt(9.80665 * 9) =  9.39468...
   *
   * Inversion of the matrix of right Eigenvectors:
   *
   *   wolframalpha.com query: invert {{1, 1}, {-sqrt(9.80665 * 9), sqrt(9.80665
   * * 9)}}
   *
   *          | 0.5  -0.0532217 |
   *   Rinv = |                 |
   *          | 0.5   0.0532217 |
   *
   * Multiplication with the jump in fluxes gives the wave strengths:
   *
   *   wolframalpha.com query: invert {{1, 1}, {-sqrt(9.80665*9),
   * sqrt(9.80665*9)}} * {0, -176.5197}
   *
   *        |  0 - 0                            |   |  9.394671362000908 |   |
   * a1 | Rinv * |                                   | = |                    |
   * = |    | | 1/2*9.80665*8^2-1/2*9.80665*10^2  |   | -9.394671362000908 |   |
   * a2 |
   *
   * The net-updates are given through the scaled eigenvectors.
   *
   *                      |  1 |   |  9.394671362000908 |
   * update #1:      a1 * |    | = |                    |
   *                      | s1 |   | -88.2598500        |
   *
   *                      |  1 |   | -9.394671362000908 |
   * update #2:      a2 * |    | = |                    |
   *                      | s2 |   | -88.2598500        |
   */

  tsunami_lab::solvers::FWave::netUpdates(10, 8, 0, 0, 0, 0, l_netUpdatesL,
                                          l_netUpdatesR);

  REQUIRE(l_netUpdatesL[0] == Approx(9.394671362000908));
  REQUIRE(l_netUpdatesL[1] == Approx(-88.25985));

  REQUIRE(l_netUpdatesR[0] == Approx(-9.394671362000908));
  REQUIRE(l_netUpdatesR[1] == Approx(-88.25985));

  /*
   * Test case (trivial steady state):
   *
   *     left | right
   *   h:  10 | 10
   *  hu:   0 |  0
   *
   * Since q_l == q_r, the flux jump is zero:
   *   delta_f = f(q_r) - f(q_l) = 0
   * Therefore all wave strengths and net-updates are zero.
   */

  tsunami_lab::solvers::FWave::netUpdates(10, 10, 0, 0, 0, 0, l_netUpdatesL,
                                          l_netUpdatesR);

  REQUIRE(l_netUpdatesL[0] == Approx(0));
  REQUIRE(l_netUpdatesL[1] == Approx(0));

  REQUIRE(l_netUpdatesR[0] == Approx(0));
  REQUIRE(l_netUpdatesR[1] == Approx(0));

  /*
   * Test case (supersonic right, lambda_{1,2} > 0):
   *
   *     left | right
   *   h:   1 | 2
   *  hu:  50 | 100
   *
   * FWave speeds are given as:
   *
   *   s1 = 50 - sqrt(9.80665 * 1.5) = 46.16464...
   *   s2 = 50 + sqrt(9.80665 * 1.5) = 53.83536...
   *
   * Inversion of the matrix of right Eigenvectors:
   *
   *   wolframalpha.com query: invert {{1, 1}, {46.16464, 53.83536}}
   *
   *          |  7.018292937299234  -0.1303658587459847 |
   *   Rinv = |                                         |
   *          | -6.018292937299234   0.1303658587459847 |
   *
   * Multiplication with the jump in fluxes gives the wave strengths:
   *
   *   wolframalpha.com query: {{7.018292937299234, -0.1303658587459847},
   * {-6.018292937299234, 0.1303658587459847}} * {50, 2514.709975}
   *
   *        | 100 - 50                                          |
   * | 23.082321476992945 |   | a1 | Rinv * | | = |                    | = | |
   *        | 100^2/2+1/2*9.80665*2^2-(50^2/1+1/2*9.80665*1^2)  |
   * | 26.917678523007055 |   | a2 |
   *
   * The net-updates are given through the scaled eigenvectors.
   *
   * Since lambda_1 > 0 and lambda_2 > 0, both waves go right -> A^- delta Q = 0
   *
   *                      |  1 |   | 23.082321476992945  |
   * update #1:      a1 * |    | = |                     |
   *                      | s1 |   | 1065.5870613496475  |
   *
   *                      |  1 |   | 26.917678523007055  |
   * update #2:      a2 * |    | = |                     |
   *                      | s2 |   | 1449.1229136503530  |
   */
  tsunami_lab::solvers::FWave::netUpdates(1, 2, 50, 100, 0, 0, l_netUpdatesL,
                                          l_netUpdatesR);

  REQUIRE(l_netUpdatesL[0] == Approx(0));
  REQUIRE(l_netUpdatesL[1] == Approx(0));

  REQUIRE(l_netUpdatesR[0] == Approx(23.082321476992945 + 26.917678523007055));
  REQUIRE(l_netUpdatesR[1] == Approx(1065.5870613496475 + 1449.1229136503530));

  /*
   * Test case (supersonic left, lambda_{1,2} < 0):
   *
   *     left | right
   *   h:    1 | 2
   *  hu:  -50 | -100
   *
   * FWave speeds are given as:
   *
   *   s1 = -50 - sqrt(9.80665 * 1.5) = -53.83535852300668
   *   s2 = -50 + sqrt(9.80665 * 1.5) = -46.16464147699332
   *
   * Symmetric to the supersonic right case with negated hu.
   * Since lambda_1 < 0 and lambda_2 < 0, both waves go left -> A^+ delta Q = 0
   *
   *   a1 = -26.91767926150345
   *   a2 = -23.08232073849655
   */
  tsunami_lab::solvers::FWave::netUpdates(1, 2, -50, -100, 0, 0, l_netUpdatesL,
                                          l_netUpdatesR);

  REQUIRE(l_netUpdatesL[0] == Approx(-26.91767926150345 + -23.08232073849655));
  REQUIRE(l_netUpdatesL[1] == Approx(1449.12291365034 + 1065.5870613496609));

  REQUIRE(l_netUpdatesR[0] == Approx(0));
  REQUIRE(l_netUpdatesR[1] == Approx(0));
}

TEST_CASE("Test the wet/dry combinations of FWave net-updates.",
         "[FWaveDryWet]") {
  float l_netUpdatesL[2] = {0};
  float l_netUpdatesR[2] = {0};

  // both sides fully dry (h = hu = 0) -> no update on either side
  tsunami_lab::solvers::FWave::netUpdates(0, 0, 0, 0, 0, 0, l_netUpdatesL,
                                          l_netUpdatesR);
  REQUIRE(l_netUpdatesL[0] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesL[1] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesR[0] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesR[1] == Approx(0).margin(1e-6));

  // both sides dry by tolerance only (0 < h <= c_dryTolerance), with nonzero
  // bathymetry -> still no update on either side
  tsunami_lab::solvers::FWave::netUpdates(0.005f, 0.008f, 0, 0, 1, 2,
                                          l_netUpdatesL, l_netUpdatesR);
  REQUIRE(l_netUpdatesL[0] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesL[1] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesR[0] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesR[1] == Approx(0).margin(1e-6));

  // left side dry -> reflects the right side; left stays untouched
  tsunami_lab::solvers::FWave::netUpdates(0, 5, 0, 3, 0, 0, l_netUpdatesL,
                                          l_netUpdatesR);
  REQUIRE(l_netUpdatesL[0] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesL[1] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesR[0] == Approx(3.0));
  REQUIRE(l_netUpdatesR[1] == Approx(21.00712585));

  // right side dry -> reflects the left side; right stays untouched
  tsunami_lab::solvers::FWave::netUpdates(5, 0, -3, 0, 0, 0, l_netUpdatesL,
                                          l_netUpdatesR);
  REQUIRE(l_netUpdatesL[0] == Approx(3.0));
  REQUIRE(l_netUpdatesL[1] == Approx(-21.00712585));
  REQUIRE(l_netUpdatesR[0] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesR[1] == Approx(0).margin(1e-6));

  // left side dry with a bathymetry step -> deltaXPsi is zeroed across the
  // step (i_bL mirrored from i_bR), left stays untouched
  tsunami_lab::solvers::FWave::netUpdates(0, 10, 0, -4, 2, 0.5f, l_netUpdatesL,
                                          l_netUpdatesR);
  REQUIRE(l_netUpdatesL[0] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesL[1] == Approx(0).margin(1e-6));
  REQUIRE(l_netUpdatesR[0] == Approx(-4.0));
  REQUIRE(l_netUpdatesR[1] == Approx(-39.61141586));

  // just above the dry threshold on the left, comfortably wet on the right
  tsunami_lab::solvers::FWave::netUpdates(0.0100001f, 5, 0.001f, 2, 0, 0,
                                          l_netUpdatesL, l_netUpdatesR);
  REQUIRE(l_netUpdatesL[0] == Approx(-11.36927700));
  REQUIRE(l_netUpdatesL[1] == Approx(51.94869995));
  REQUIRE(l_netUpdatesR[0] == Approx(13.36827660));
  REQUIRE(l_netUpdatesR[1] == Approx(71.43383026));
}
