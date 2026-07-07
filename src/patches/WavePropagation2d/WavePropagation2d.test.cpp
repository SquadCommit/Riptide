/**
 * @author Yannik Köllmann (yannik.koellmann AT uni-jena.de)
 * @author Jan Vogt (jan.vogt AT uni-jena.de)
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Unit tests for the two-dimensional wave propagation patch.
 *
 * Note: WavePropagation2d uses getCoordinates(x, y) = x + y * (ny+2), which
 * is only collision-free for square grids (nx == ny). All tests therefore use
 * square grids, matching how main.cpp always sets nx == ny.
 **/
#include "WavePropagation2d.h"
#include <catch2/catch.hpp>

TEST_CASE("2D x-direction dam break propagates correctly (FWave).",
          "[WaveProp2dXSweep]") {
  /*
   * 100x100 grid. Dam break between inner ix=49 and ix=50; h is uniform in y.
   *   ix  0-49 : h=10, hu=0, hv=0
   *   ix 50-99 : h=8,  hu=0, hv=0
   *
   * FWave net-updates at edge 49|50 (same values as 1D):
   *   left  cell: dh = +9.394671362000908, dhu = -88.2598500
   *   right cell: dh = -9.394671362000908, dhu = -88.2598500
   *
   * The y-sweep contributes zero because h is uniform within every column.
   */
  tsunami_lab::patches::WavePropagation2d l_waveProp(100, 100);
  tsunami_lab::t_idx l_stride = l_waveProp.getStride(); // 102

  for (tsunami_lab::t_idx l_iy = 0; l_iy < 100; l_iy++) {
    for (tsunami_lab::t_idx l_ix = 0; l_ix < 50; l_ix++)
      l_waveProp.setHeight(l_ix, l_iy, 10);
    for (tsunami_lab::t_idx l_ix = 50; l_ix < 100; l_ix++)
      l_waveProp.setHeight(l_ix, l_iy, 8);
  }

  l_waveProp.setGhostOutflow();
  l_waveProp.timeStep(0.1, "FWAVE");

  // getHeight()[iy * stride + ix] accesses inner cell (ix, iy)
  // check first inner row (iy=0) only; all rows are identical by symmetry

  // steady state
  for (tsunami_lab::t_idx l_ix = 0; l_ix < 49; l_ix++)
    REQUIRE(l_waveProp.getHeight()[l_ix] == Approx(10));

  // dam-break cells
  REQUIRE(l_waveProp.getHeight()[49] == Approx(10 - 0.1 * 9.394671362000908));
  REQUIRE(l_waveProp.getMomentumX()[49] == Approx(0 + 0.1 * 88.2598500));
  REQUIRE(l_waveProp.getMomentumY()[49] == Approx(0)); // hv untouched

  REQUIRE(l_waveProp.getHeight()[50] == Approx(8 + 0.1 * 9.394671362000908));
  REQUIRE(l_waveProp.getMomentumX()[50] == Approx(0 + 0.1 * 88.2598500));
  REQUIRE(l_waveProp.getMomentumY()[50] == Approx(0));

  // steady state
  for (tsunami_lab::t_idx l_ix = 51; l_ix < 100; l_ix++)
    REQUIRE(l_waveProp.getHeight()[l_ix] == Approx(8));

  (void)l_stride;
}

TEST_CASE("2D y-direction dam break propagates correctly (FWave).",
          "[WaveProp2dYSweep]") {
  /*
   * 100x100 grid. Dam break between inner iy=49 and iy=50; h is uniform in x.
   *   iy  0-49 : h=10, hu=0, hv=0
   *   iy 50-99 : h=8,  hu=0, hv=0
   *
   * FWave net-updates at edge 49|50:
   *   bottom cell: dh = +9.394671362000908, dhv = -88.2598500
   *   top    cell: dh = -9.394671362000908, dhv = -88.2598500
   *
   * The x-sweep contributes zero because h is uniform within every row.
   */
  tsunami_lab::patches::WavePropagation2d l_waveProp(100, 100);
  tsunami_lab::t_idx l_stride = l_waveProp.getStride(); // 102

  for (tsunami_lab::t_idx l_ix = 0; l_ix < 100; l_ix++) {
    for (tsunami_lab::t_idx l_iy = 0; l_iy < 50; l_iy++)
      l_waveProp.setHeight(l_ix, l_iy, 10);
    for (tsunami_lab::t_idx l_iy = 50; l_iy < 100; l_iy++)
      l_waveProp.setHeight(l_ix, l_iy, 8);
  }

  l_waveProp.setGhostOutflow();
  l_waveProp.timeStep(0.1, "FWAVE");

  // check first inner column (ix=0) only; all columns are identical by symmetry

  // steady state
  for (tsunami_lab::t_idx l_iy = 0; l_iy < 49; l_iy++)
    REQUIRE(l_waveProp.getHeight()[l_iy * l_stride] == Approx(10));

  // dam-break cells
  REQUIRE(l_waveProp.getHeight()[49 * l_stride] ==
          Approx(10 - 0.1 * 9.394671362000908));
  REQUIRE(l_waveProp.getMomentumY()[49 * l_stride] ==
          Approx(0 + 0.1 * 88.2598500));
  REQUIRE(l_waveProp.getMomentumX()[49 * l_stride] ==
          Approx(0)); // hu untouched

  REQUIRE(l_waveProp.getHeight()[50 * l_stride] ==
          Approx(8 + 0.1 * 9.394671362000908));
  REQUIRE(l_waveProp.getMomentumY()[50 * l_stride] ==
          Approx(0 + 0.1 * 88.2598500));
  REQUIRE(l_waveProp.getMomentumX()[50 * l_stride] == Approx(0));

  // steady state
  for (tsunami_lab::t_idx l_iy = 51; l_iy < 100; l_iy++)
    REQUIRE(l_waveProp.getHeight()[l_iy * l_stride] == Approx(8));
}

TEST_CASE("2D reflecting boundary conditions mirror the adjacent cell.",
          "[WaveProp2dReflecting]") {
  /*
   * 4x4 square domain.  stride = ny+2 = 6.
   * h(ix,iy)  = ix+1   (1..4)
   * hu(ix,iy) = ix+2   (2..5)
   * hv(ix,iy) = iy+2   (2..5)
   *
   * Ghost-cell offsets relative to getHeight() (which points to inner (0,0)):
   *   left ghost  of row iy=0 : index -1
   *   right ghost of row iy=0 : index  nx = 4
   *   bottom ghost of col ix=0: index -stride = -6
   *   top    ghost of col ix=0: index  ny*stride = 24
   */
  tsunami_lab::patches::WavePropagation2d l_waveProp(4, 4);
  tsunami_lab::t_idx l_stride = l_waveProp.getStride(); // 6

  for (tsunami_lab::t_idx l_iy = 0; l_iy < 4; l_iy++)
    for (tsunami_lab::t_idx l_ix = 0; l_ix < 4; l_ix++) {
      l_waveProp.setHeight(l_ix, l_iy,
                           static_cast<tsunami_lab::t_real>(l_ix + 1));
      l_waveProp.setMomentumX(l_ix, l_iy,
                              static_cast<tsunami_lab::t_real>(l_ix + 2));
      l_waveProp.setMomentumY(l_ix, l_iy,
                              static_cast<tsunami_lab::t_real>(l_iy + 2));
    }

  using BC = tsunami_lab::patches::BoundaryCondition;
  const BC Out = BC::Outflow;
  const BC Ref = BC::Reflecting;

  SECTION("reflecting left: hu negated, hv unchanged") {
    l_waveProp.setGhost(Ref, Out, Out, Out);
    // mirrors inner (ix=0, iy=0): h=1, hu=2, hv=2
    REQUIRE(l_waveProp.getHeight()[-1] == Approx(1));
    REQUIRE(l_waveProp.getMomentumX()[-1] == Approx(-2));
    REQUIRE(l_waveProp.getMomentumY()[-1] == Approx(2));
  }

  SECTION("reflecting right: hu negated, hv unchanged") {
    l_waveProp.setGhost(Out, Ref, Out, Out);
    // mirrors inner (ix=3, iy=0): h=4, hu=5, hv=2
    REQUIRE(l_waveProp.getHeight()[4] == Approx(4));
    REQUIRE(l_waveProp.getMomentumX()[4] == Approx(-5));
    REQUIRE(l_waveProp.getMomentumY()[4] == Approx(2));
  }

  SECTION("reflecting lower: hv negated, hu unchanged") {
    l_waveProp.setGhost(Out, Out, Out, Ref);
    // mirrors inner (ix=0, iy=0): h=1, hu=2, hv=2
    REQUIRE(l_waveProp.getHeight()[-(tsunami_lab::t_idx)l_stride] == Approx(1));
    REQUIRE(l_waveProp.getMomentumY()[-(tsunami_lab::t_idx)l_stride] ==
            Approx(-2));
    REQUIRE(l_waveProp.getMomentumX()[-(tsunami_lab::t_idx)l_stride] ==
            Approx(2));
  }

  SECTION("reflecting upper: hv negated, hu unchanged") {
    l_waveProp.setGhost(Out, Out, Ref, Out);
    // mirrors inner (ix=0, iy=3): h=1, hu=2, hv=5
    REQUIRE(l_waveProp.getHeight()[4 * (tsunami_lab::t_idx)l_stride] ==
            Approx(1));
    REQUIRE(l_waveProp.getMomentumY()[4 * (tsunami_lab::t_idx)l_stride] ==
            Approx(-5));
    REQUIRE(l_waveProp.getMomentumX()[4 * (tsunami_lab::t_idx)l_stride] ==
            Approx(2));
  }

  SECTION("setGhostOutflow copies without negation on all four sides") {
    l_waveProp.setGhostOutflow();
    // left: mirrors (ix=0, iy=0) -> h=1, hu=2, hv=2
    REQUIRE(l_waveProp.getHeight()[-1] == Approx(1));
    REQUIRE(l_waveProp.getMomentumX()[-1] == Approx(2));
    // right: mirrors (ix=3, iy=0) -> h=4, hu=5
    REQUIRE(l_waveProp.getHeight()[4] == Approx(4));
    REQUIRE(l_waveProp.getMomentumX()[4] == Approx(5));
    // lower: mirrors (ix=0, iy=0) -> hv=2
    REQUIRE(l_waveProp.getMomentumY()[-(tsunami_lab::t_idx)l_stride] ==
            Approx(2));
    // upper: mirrors (ix=0, iy=3) -> hv=5
    REQUIRE(l_waveProp.getMomentumY()[4 * (tsunami_lab::t_idx)l_stride] ==
            Approx(5));
  }
}
TEST_CASE("2D time step flushes momentum in nearly-dry cells.",
          "[WaveProp2dDryFlush]") {
  /*
   * A cell whose height is at or below c_dryTolerance gets no updates from
   * the (masked) FWave solver, so leftover momentum would stay frozen
   * forever. That frozen momentum poisons CFL estimates (u = hu/h with a
   * tiny h) and would re-enter the dynamics unchanged if the cell rewets.
   * The clamp phase must therefore zero hu/hv wherever h <= c_dryTolerance.
   */
  tsunami_lab::patches::WavePropagation2d l_waveProp(16, 16);
  tsunami_lab::t_idx l_stride = l_waveProp.getStride();

  // still lake, 5 m deep
  for (tsunami_lab::t_idx l_iy = 0; l_iy < 16; l_iy++) {
    for (tsunami_lab::t_idx l_ix = 0; l_ix < 16; l_ix++) {
      l_waveProp.setBathymetry(l_ix, l_iy, -5.0f);
      l_waveProp.setHeight(l_ix, l_iy, 5.0f);
      l_waveProp.setMomentumX(l_ix, l_iy, 0.0f);
      l_waveProp.setMomentumY(l_ix, l_iy, 0.0f);
    }
  }

  // nearly-dry cell with stale momentum (drying coastal cell)
  l_waveProp.setBathymetry(7, 7, 0.0f);
  l_waveProp.setHeight(7, 7, 0.005f); // <= c_dryTolerance = 0.01
  l_waveProp.setMomentumX(7, 7, 3.0f);
  l_waveProp.setMomentumY(7, 7, -2.0f);

  l_waveProp.setGhostOutflow();
  l_waveProp.timeStep(0.001f, "FWAVE");

  const tsunami_lab::t_idx l_i = 7 + 7 * l_stride;
  REQUIRE(l_waveProp.getMomentumX()[l_i] == 0.0f);
  REQUIRE(l_waveProp.getMomentumY()[l_i] == 0.0f);
  // the dry cell's height is untouched (no updates, no artificial wetting)
  REQUIRE(l_waveProp.getHeight()[l_i] == Approx(0.005f));
}
