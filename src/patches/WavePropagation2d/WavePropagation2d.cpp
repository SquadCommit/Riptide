/**
 * @author Yannik Köllmann
 * @author Jan Vogt
 * @author Mika Brückner
 * @section DESCRIPTION
 * Two-dimensional wave propagation patch.
 **/

#include "WavePropagation2d.h"
#include "../../solvers/fwave/FWave.h"
#include <algorithm>
#include <cmath>

tsunami_lab::patches::WavePropagation2d::WavePropagation2d(t_idx i_nCells_x,
                                                           t_idx i_nCells_y) {
  m_nCells_x = i_nCells_x;
  m_nCells_y = i_nCells_y;

  t_idx l_size = (m_nCells_x + 2) * (m_nCells_y + 2);

  // Allocate without zero-init; touch pages in parallel (NUMA first-touch) so
  // each page is mapped on the NUMA node of the thread that will own it during
  // the sweeps.
  m_h = new t_real[2 * l_size];
  m_hu = new t_real[2 * l_size];
  m_hv = new t_real[2 * l_size];
  m_b = new t_real[l_size];

#pragma omp parallel for schedule(static)
  for (t_idx l_i = 0; l_i < l_size; l_i++) {
    m_h[l_i] = 0;
    m_h[l_size + l_i] = 0;
    m_hu[l_i] = 0;
    m_hu[l_size + l_i] = 0;
    m_hv[l_i] = 0;
    m_hv[l_size + l_i] = 0;
    m_b[l_i] = 0;
  }
}

tsunami_lab::patches::WavePropagation2d::~WavePropagation2d() {
  delete[] m_h;
  delete[] m_hu;
  delete[] m_hv;
  delete[] m_b;
}

void tsunami_lab::patches::WavePropagation2d::setGhostOutflow() {
  t_idx l_nY = m_nCells_y + 2;
  t_idx l_stepSize = (m_nCells_x + 2) * l_nY;

  t_real* l_h = m_h + m_step * l_stepSize;
  t_real* l_hu = m_hu + m_step * l_stepSize;
  t_real* l_hv = m_hv + m_step * l_stepSize;

  // Left ghost column (x=0): copy from x=1
  for (t_idx l_iy = 0; l_iy < l_nY; l_iy++) {
    t_idx l_ghost = getCoordinates(0, l_iy);
    t_idx l_inner = getCoordinates(1, l_iy);
    l_h[l_ghost] = l_h[l_inner];
    l_hu[l_ghost] = l_hu[l_inner];
    l_hv[l_ghost] = l_hv[l_inner];
    m_b[l_ghost] = m_b[l_inner];
  }

  // Right ghost column (x = m_nCells_x+1): copy from x = m_nCells_x
  for (t_idx l_iy = 0; l_iy < l_nY; l_iy++) {
    t_idx l_ghost = getCoordinates(m_nCells_x + 1, l_iy);
    t_idx l_inner = getCoordinates(m_nCells_x, l_iy);
    l_h[l_ghost] = l_h[l_inner];
    l_hu[l_ghost] = l_hu[l_inner];
    l_hv[l_ghost] = l_hv[l_inner];
    m_b[l_ghost] = m_b[l_inner];
  }

  // Bottom ghost row (y=0): copy from y=1
  for (t_idx l_ix = 0; l_ix < m_nCells_x + 2; l_ix++) {
    t_idx l_ghost = getCoordinates(l_ix, 0);
    t_idx l_inner = getCoordinates(l_ix, 1);
    l_h[l_ghost] = l_h[l_inner];
    l_hu[l_ghost] = l_hu[l_inner];
    l_hv[l_ghost] = l_hv[l_inner];
    m_b[l_ghost] = m_b[l_inner];
  }

  // Top ghost row (y = m_nCells_y+1): copy from y = m_nCells_y
  for (t_idx l_ix = 0; l_ix < m_nCells_x + 2; l_ix++) {
    t_idx l_ghost = getCoordinates(l_ix, m_nCells_y + 1);
    t_idx l_inner = getCoordinates(l_ix, m_nCells_y);
    l_h[l_ghost] = l_h[l_inner];
    l_hu[l_ghost] = l_hu[l_inner];
    l_hv[l_ghost] = l_hv[l_inner];
    m_b[l_ghost] = m_b[l_inner];
  }
}

void tsunami_lab::patches::WavePropagation2d::setGhost(
    BoundaryCondition i_left,
    BoundaryCondition i_right,
    BoundaryCondition i_upper,
    BoundaryCondition i_lower) {
  t_idx l_nY = m_nCells_y + 2;
  t_idx l_stepSize = (m_nCells_x + 2) * l_nY;
  t_real* l_h = m_h + m_step * l_stepSize;
  t_real* l_hu = m_hu + m_step * l_stepSize;
  t_real* l_hv = m_hv + m_step * l_stepSize;

  for (t_idx l_iy = 0; l_iy < l_nY; l_iy++) {
    t_idx l_ghost = getCoordinates(0, l_iy);
    t_idx l_inner = getCoordinates(1, l_iy);
    l_h[l_ghost] = l_h[l_inner];
    l_hu[l_ghost] = (i_left == BoundaryCondition::Reflecting) ? -l_hu[l_inner]
                                                              : l_hu[l_inner];
    l_hv[l_ghost] = l_hv[l_inner];
    m_b[l_ghost] = m_b[l_inner];
  }

  for (t_idx l_iy = 0; l_iy < l_nY; l_iy++) {
    t_idx l_ghost = getCoordinates(m_nCells_x + 1, l_iy);
    t_idx l_inner = getCoordinates(m_nCells_x, l_iy);
    l_h[l_ghost] = l_h[l_inner];
    l_hu[l_ghost] = (i_right == BoundaryCondition::Reflecting) ? -l_hu[l_inner]
                                                               : l_hu[l_inner];
    l_hv[l_ghost] = l_hv[l_inner];
    m_b[l_ghost] = m_b[l_inner];
  }

  for (t_idx l_ix = 0; l_ix < m_nCells_x + 2; l_ix++) {
    t_idx l_ghost = getCoordinates(l_ix, 0);
    t_idx l_inner = getCoordinates(l_ix, 1);
    l_h[l_ghost] = l_h[l_inner];
    l_hu[l_ghost] = l_hu[l_inner];
    l_hv[l_ghost] = (i_lower == BoundaryCondition::Reflecting) ? -l_hv[l_inner]
                                                               : l_hv[l_inner];
    m_b[l_ghost] = m_b[l_inner];
  }

  for (t_idx l_ix = 0; l_ix < m_nCells_x + 2; l_ix++) {
    t_idx l_ghost = getCoordinates(l_ix, m_nCells_y + 1);
    t_idx l_inner = getCoordinates(l_ix, m_nCells_y);
    l_h[l_ghost] = l_h[l_inner];
    l_hu[l_ghost] = l_hu[l_inner];
    l_hv[l_ghost] = (i_upper == BoundaryCondition::Reflecting) ? -l_hv[l_inner]
                                                               : l_hv[l_inner];
    m_b[l_ghost] = m_b[l_inner];
  }
}

void tsunami_lab::patches::WavePropagation2d::timeStep(t_real i_scaling,
                                                       std::string) {
  t_idx l_size = (m_nCells_x + 2) * (m_nCells_y + 2);

  // __restrict: cur/new point into disjoint halves of the double buffers,
  // which the compiler can't prove on its own; without it the sweep loops
  // don't auto-vectorize.
  const t_real* __restrict l_hCur = m_h + m_step * l_size;
  const t_real* __restrict l_huCur = m_hu + m_step * l_size;
  const t_real* __restrict l_hvCur = m_hv + m_step * l_size;

  m_step = 1 - m_step;

  t_real* __restrict l_hNew = m_h + m_step * l_size;
  t_real* __restrict l_huNew = m_hu + m_step * l_size;
  t_real* __restrict l_hvNew = m_hv + m_step * l_size;
  const t_real* __restrict l_b = m_b;

  t_idx l_stride = getStride();
  t_idx l_nx = m_nCells_x;
  t_idx l_ny = m_nCells_y;

  // One parallel region for all three phases to avoid repeated thread creation.
#pragma omp parallel
  {
    // Per-thread edge buffers for the x-sweep: net updates are first stored
    // per edge, then applied to the cells in a second loop. This removes the
    // read-write overlap on cell ix+1 between consecutive edges, so both the
    // edge loop and the apply loop auto-vectorize (the branch-free FWave
    // kernel compiles to selects). R-going updates are stored shifted by +1,
    // making the apply loop a plain elementwise sum with sentinel zeros at
    // the ends.
    std::vector<t_real> l_edgeBuf(4 * l_stride, 0);
    t_real* __restrict l_eLh = l_edgeBuf.data();
    t_real* __restrict l_eLhu = l_edgeBuf.data() + l_stride;
    t_real* __restrict l_eRh = l_edgeBuf.data() + 2 * l_stride;
    t_real* __restrict l_eRhu = l_edgeBuf.data() + 3 * l_stride;

    // X-sweep fused with the copy: each row is written once (cur - updates)
    // while cache-hot; ghost rows only need the copy. Rows are independent.
#pragma omp for schedule(runtime)
    for (t_idx l_iy = 0; l_iy < l_ny + 2; l_iy++) {
      t_idx l_row = l_iy * l_stride;

      if (l_iy == 0 || l_iy > l_ny) { // ghost row: copy only
        for (t_idx l_i = l_row; l_i < l_row + l_stride; l_i++) {
          l_hNew[l_i] = l_hCur[l_i];
          l_huNew[l_i] = l_huCur[l_i];
          l_hvNew[l_i] = l_hvCur[l_i];
        }
        continue;
      }

      // edge loop: edge l_ix sits between cells l_ix and l_ix+1
      for (t_idx l_ix = 0; l_ix <= l_nx; l_ix++) {
        t_real l_netUpdateL[2];
        t_real l_netUpdateR[2];

        solvers::FWave::netUpdates(
            l_hCur[l_row + l_ix], l_hCur[l_row + l_ix + 1],
            l_huCur[l_row + l_ix], l_huCur[l_row + l_ix + 1],
            l_b[l_row + l_ix], l_b[l_row + l_ix + 1], l_netUpdateL,
            l_netUpdateR);

        l_eLh[l_ix] = l_netUpdateL[0];
        l_eLhu[l_ix] = l_netUpdateL[1];
        l_eRh[l_ix + 1] = l_netUpdateR[0];
        l_eRhu[l_ix + 1] = l_netUpdateR[1];
      }

      // apply loop: cell l_ix takes the L-update of edge l_ix and the
      // R-update of edge l_ix-1 (stored shifted at l_ix). l_eRh[0] and
      // l_eLh[l_stride-1] stay zero from initialization.
      for (t_idx l_ix = 0; l_ix < l_stride; l_ix++) {
        l_hNew[l_row + l_ix] =
            l_hCur[l_row + l_ix] - i_scaling * (l_eLh[l_ix] + l_eRh[l_ix]);
        l_huNew[l_row + l_ix] =
            l_huCur[l_row + l_ix] - i_scaling * (l_eLhu[l_ix] + l_eRhu[l_ix]);
        l_hvNew[l_row + l_ix] = l_hvCur[l_row + l_ix];
      }
    }
    // Y-sweep: adjacent edges share cells, so use red-black (even/odd) row
    // splitting — edges of the same parity write disjoint cells and can run
    // in parallel; the implicit barrier between passes orders the shared
    // writes. Within one edge row there is no overlap between iterations,
    // so the loop vectorizes directly (no buffers needed).
    auto l_processYRow = [&](t_idx l_iy) {
      t_idx l_rowB = l_iy * l_stride;
      t_idx l_rowT = l_rowB + l_stride;
      for (t_idx l_ix = 1; l_ix <= l_nx; l_ix++) {
        t_real l_netUpdateB[2];
        t_real l_netUpdateT[2];

        solvers::FWave::netUpdates(
            l_hCur[l_rowB + l_ix], l_hCur[l_rowT + l_ix],
            l_hvCur[l_rowB + l_ix], l_hvCur[l_rowT + l_ix], l_b[l_rowB + l_ix],
            l_b[l_rowT + l_ix], l_netUpdateB, l_netUpdateT);

        l_hNew[l_rowB + l_ix] -= i_scaling * l_netUpdateB[0];
        l_hvNew[l_rowB + l_ix] -= i_scaling * l_netUpdateB[1];
        l_hNew[l_rowT + l_ix] -= i_scaling * l_netUpdateT[0];
        l_hvNew[l_rowT + l_ix] -= i_scaling * l_netUpdateT[1];
      }
    };

#pragma omp for schedule(runtime)
    for (t_idx l_iy = 0; l_iy <= m_nCells_y; l_iy += 2) {
      l_processYRow(l_iy);
    }

    // Black phase with fused clamp: after the odd edge l_iy is processed,
    // rows l_iy and l_iy+1 have received all their updates (their other
    // y-edges are even and were handled in the red phase). Clamp h >= 0 and
    // reset any non-finite cell (NaN/Inf) to dry while the rows are still
    // cache-hot, so a locally unstable cell can't keep infecting its
    // neighbours via the stencil. This covers all interior rows; ghost rows
    // are re-derived from clamped interior rows by setGhost*() before the
    // next time step.
#pragma omp for schedule(runtime)
    for (t_idx l_iy = 1; l_iy <= m_nCells_y; l_iy += 2) {
      l_processYRow(l_iy);

      t_idx l_end = std::min((l_iy + 2) * l_stride, l_size);
      for (t_idx l_i = l_iy * l_stride; l_i < l_end; l_i++) {
        bool l_invalid = l_hNew[l_i] < 0 || !std::isfinite(l_hNew[l_i]) ||
                         !std::isfinite(l_huNew[l_i]) ||
                         !std::isfinite(l_hvNew[l_i]);
        if (l_invalid) {
          l_hNew[l_i] = 0;
        }
        // Flush momentum in dry cells (h <= c_dryTolerance): the solver
        // masks their updates, so leftover momentum would stay frozen
        // forever — it poisons CFL estimates (u = hu/h with tiny h) and
        // would re-enter the dynamics unchanged if the cell rewets.
        if (l_invalid || l_hNew[l_i] <= c_dryTolerance) {
          l_huNew[l_i] = 0;
          l_hvNew[l_i] = 0;
        }
      }
    }
  } // end parallel
}
