/**
 * @author Jan Vogt (jan.vogt AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Unit tests for the SimBuffer double buffer and the SolverThread background
 * solver. Grids are square (nx == ny) because WavePropagation2d's flat
 * indexing is only collision-free for square grids.
 **/
#include "SolverThread.h"
#include "SimBuffer.h"

#include <catch2/catch.hpp>
#include <chrono>
#include <cmath>
#include <thread>

using namespace tsunami_lab;

TEST_CASE("SimBuffer publishes frames front/back via swap.", "[SimBuffer]") {
  visualization::SimBuffer l_buf(2, 2); // 4 cells

  // Nothing written yet: swap() reports no fresh frame.
  REQUIRE_FALSE(l_buf.swap());

  const t_real l_a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  l_buf.write(l_a, 4);
  REQUIRE(l_buf.swap()); // fresh frame available
  const t_real* l_f = l_buf.front();
  REQUIRE(l_f[0] == Approx(1.0f));
  REQUIRE(l_f[1] == Approx(2.0f));
  REQUIRE(l_f[2] == Approx(3.0f));
  REQUIRE(l_f[3] == Approx(4.0f));

  // No new write since the last swap → swap() is a no-op.
  REQUIRE_FALSE(l_buf.swap());

  // A second frame supersedes the first.
  const t_real l_b[4] = {5.0f, 6.0f, 7.0f, 8.0f};
  l_buf.write(l_b, 4);
  REQUIRE(l_buf.swap());
  l_f = l_buf.front();
  REQUIRE(l_f[0] == Approx(5.0f));
  REQUIRE(l_f[3] == Approx(8.0f));
}

TEST_CASE("WavePropagation2d indexes rectangular grids (nx != ny) correctly.",
          "[SolverThread][rectangular]") {
  // An x-only initial condition (uniform in y) must stay perfectly uniform in
  // y on a non-square grid. If the flat indexing used the wrong stride, rows
  // would bleed into each other and this invariance would break.
  constexpr t_idx l_nx = 60;
  constexpr t_idx l_ny = 30; // deliberately nx != ny
  patches::WavePropagation2d l_solver(l_nx, l_ny);

  for (t_idx l_y = 0; l_y < l_ny; l_y++)
    for (t_idx l_x = 0; l_x < l_nx; l_x++) {
      l_solver.setBathymetry(l_x, l_y, -10.0f);
      l_solver.setMomentumX(l_x, l_y, 0.0f);
      l_solver.setMomentumY(l_x, l_y, 0.0f);
      l_solver.setHeight(l_x, l_y, (l_x < l_nx / 2) ? 8.0f : 4.0f);
    }

  for (int l_step = 0; l_step < 30; l_step++) {
    l_solver.setGhost(patches::BoundaryCondition::Outflow,
                      patches::BoundaryCondition::Outflow);
    l_solver.timeStep(0.01f, "fwave");
  }

  const t_real* l_h = l_solver.getHeight();
  const t_idx l_stride = l_solver.getStride();
  for (t_idx l_x = 0; l_x < l_nx; l_x++) {
    const t_real l_ref = l_h[l_x]; // row iy = 0
    for (t_idx l_y = 0; l_y < l_ny; l_y++) {
      const t_real l_v = l_h[l_x + l_y * l_stride];
      REQUIRE(std::isfinite(l_v));
      REQUIRE(l_v == Approx(l_ref)); // y-invariance preserved
    }
  }
}

TEST_CASE("SolverThread advances a still lake with a bump and publishes it.",
          "[SolverThread]") {
  constexpr t_idx l_n = 50; // square grid
  constexpr t_real l_dxy = 1000.0f;
  constexpr t_real l_depth = 100.0f; // bathymetry = -100 m, surface at 0

  visualization::SimBuffer l_buf(l_n, l_n);
  visualization::SolverThread l_sim(l_buf, l_n, l_n, l_dxy);

  // Flat ocean floor; still water (h = -b); a 5 m bump in the centre.
  auto& l_solver = l_sim.solver();
  for (t_idx l_y = 0; l_y < l_n; l_y++) {
    for (t_idx l_x = 0; l_x < l_n; l_x++) {
      l_solver.setBathymetry(l_x, l_y, -l_depth);
      l_solver.setMomentumX(l_x, l_y, 0.0f);
      l_solver.setMomentumY(l_x, l_y, 0.0f);
      const bool l_centre = (l_x >= 23 && l_x <= 26 && l_y >= 23 && l_y <= 26);
      l_solver.setHeight(l_x, l_y, l_depth + (l_centre ? 5.0f : 0.0f));
    }
  }

  l_sim.start();
  REQUIRE(l_sim.running());
  REQUIRE(l_sim.scaling() > 0.0f); // CFL step was derived

  // Wait until the loop has advanced a few steps (timeout guards against
  // hangs).
  const auto l_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (l_sim.steps() < 20 && std::chrono::steady_clock::now() < l_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  l_sim.stop();

  REQUIRE_FALSE(l_sim.running());
  REQUIRE(l_sim.steps() >= 20);

  // A frame must be available and physically sane: every cell stays wet and
  // finite, and the central bump has spread (centre no longer at 105 m).
  REQUIRE(l_buf.swap());
  const t_real* l_h = l_buf.front();
  for (t_idx l_i = 0; l_i < l_n * l_n; l_i++) {
    REQUIRE(std::isfinite(l_h[l_i]));
    REQUIRE(l_h[l_i] > 0.0f);
    REQUIRE(l_h[l_i] < 200.0f);
  }
  const t_real l_centre = l_h[24 + 24 * l_n];
  REQUIRE(l_centre != Approx(l_depth + 5.0f));
}

TEST_CASE("CFL scaling ignores nearly-dry cells with stale momentum.",
          "[SolverThread][DryCFL]") {
  /*
   * Regression test: a drying coastal cell (h <= c_dryTolerance) with
   * leftover momentum used to enter maxWaveSpeed() via u = hu/h with a tiny
   * h, collapsing dt (and with it the visualization's time-scale factor) for
   * the rest of the run. Dry cells carry no waves and must not dictate the
   * time step.
   */
  constexpr t_idx l_n = 20;
  constexpr t_real l_dxy = 1000.0f;
  constexpr t_real l_depth = 100.0f;

  visualization::SimBuffer l_buf(l_n, l_n);
  visualization::SolverThread l_sim(l_buf, l_n, l_n, l_dxy);

  auto& l_solver = l_sim.solver();
  for (t_idx l_y = 0; l_y < l_n; l_y++) {
    for (t_idx l_x = 0; l_x < l_n; l_x++) {
      l_solver.setBathymetry(l_x, l_y, -l_depth);
      l_solver.setHeight(l_x, l_y, l_depth);
      l_solver.setMomentumX(l_x, l_y, 0.0f);
      l_solver.setMomentumY(l_x, l_y, 0.0f);
    }
  }
  // nearly-dry cell with stale momentum: u = 1 / 0.001 = 1000 m/s if counted
  l_solver.setBathymetry(10, 10, 0.0f);
  l_solver.setHeight(10, 10, 0.001f);
  l_solver.setMomentumX(10, 10, 1.0f);

  l_sim.start();
  const t_real l_scaling = l_sim.scaling();
  l_sim.stop();

  // wet-lake wave speed: sqrt(g * 100) ~ 31.3 m/s -> scaling ~ 0.45 / 31.3.
  // If the dry cell were counted (speed ~ 1000 m/s), scaling would be ~30x
  // smaller.
  REQUIRE(l_scaling > 0.45f / 40.0f);
  REQUIRE(l_scaling <= 0.45f / 31.0f);
}
