/**
 * @author Jan Vogt (jan.vogt AT uni-jena.de)
 **/
#include "SolverThread.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace tsunami_lab {
namespace visualization {

SolverThread::SolverThread(SimBuffer& io_buffer,
                           t_idx i_nx,
                           t_idx i_ny,
                           t_real i_dxy)
    : m_buffer(io_buffer), m_solver(i_nx, i_ny), m_nx(i_nx), m_ny(i_ny),
      m_dxy(i_dxy) {}

SolverThread::~SolverThread() { stop(); }

t_real SolverThread::maxWaveSpeed() {
  const t_real* l_h = m_solver.getHeight();
  const t_real* l_hu = m_solver.getMomentumX();
  const t_real* l_hv = m_solver.getMomentumY();
  const t_idx l_stride = m_solver.getStride();

  t_real l_speedMax = 0;
  for (t_idx l_y = 0; l_y < m_ny; l_y++) {
    for (t_idx l_x = 0; l_x < m_nx; l_x++) {
      const t_idx l_i = l_x + l_y * l_stride;
      const t_real l_hc = l_h[l_i];
      // Skip cells the solver treats as dry (h <= c_dryTolerance), not just
      // h <= 0: a nearly-dry cell with leftover momentum yields a huge
      // spurious u = hu/h that would collapse the CFL time step for the
      // whole run even though it carries no waves.
      if (l_hc <= c_dryTolerance)
        continue;
      const t_real l_u = std::abs(l_hu[l_i] / l_hc);
      const t_real l_v = std::abs(l_hv[l_i] / l_hc);
      const t_real l_speed = std::max(l_u, l_v) + std::sqrt(g * l_hc);
      l_speedMax = std::max(l_speedMax, l_speed);
    }
  }
  return l_speedMax;
}

void SolverThread::start() {
  if (m_running.load())
    return;

  // 0.45 keeps a margin below the CFL limit of the dimensionally-split scheme.
  constexpr t_real l_cfl = t_real(0.45);
  const t_real l_speedMax = maxWaveSpeed();
  m_scaling.store((l_speedMax > t_real(0)) ? l_cfl / l_speedMax : t_real(0));

  m_steps.store(0);
  m_simTimeAccum.store(0.0);
  m_running.store(true);
  m_thread = std::thread(&SolverThread::run, this);
}

void SolverThread::stop() {
  if (m_thread.joinable()) {
    m_running.store(false);
    m_thread.join();
  }
}

void SolverThread::run() {
  // 0.45 keeps a margin below the CFL limit of the dimensionally-split scheme.
  constexpr t_real l_cfl = t_real(0.45);

  std::vector<t_real> l_frame(m_nx * m_ny);
  const t_idx l_stride = m_solver.getStride();
  std::chrono::steady_clock::time_point l_next =
      std::chrono::steady_clock::now();

  while (m_running.load()) {
    const auto l_t0 = std::chrono::steady_clock::now();

    // re-derive the CFL-stable scaling from the current state every step
    const t_real l_speedMax = maxWaveSpeed();
    const t_real l_scaling =
        (l_speedMax > t_real(0)) ? l_cfl / l_speedMax : t_real(0);
    m_scaling.store(l_scaling);
    const double l_dt = (double)l_scaling * (double)m_dxy;

    m_solver.setGhost(patches::BoundaryCondition::Outflow,
                      patches::BoundaryCondition::Outflow);
    m_solver.timeStep(l_scaling, m_mode);

    // Froude-limit the velocity in wet cells. At the wet/dry front a thin
    // cell (h just above c_dryTolerance) can pick up a spurious u = hu/h of
    // hundreds to thousands of m/s. The CFL step correctly shrinks to stay
    // stable, but after long inundating runs that single artefact drags the
    // whole simulation down to a crawl (dt -> ~0). Capping |u| at
    // k_maxFroude * sqrt(g*h) removes it in the dynamics, so both the F-wave
    // scheme and the next maxWaveSpeed() see a physical velocity and dt stays
    // healthy — without capping the CFL estimate itself, which would leave
    // the true velocity uncapped and let the scheme diverge. Deep water has
    // Fr ~ 0 and is untouched; genuine flow up to this Froude number
    // (supercritical bores included) is preserved. Writes go to the buffer
    // getMomentum*() reads (the post-step m_step half), which the next step
    // consumes; setMomentum*() would target the wrong half.
    constexpr t_real k_maxFroude = t_real(4);
    {
      const t_real* l_hArr = m_solver.getHeight();
      t_real* l_huArr = const_cast<t_real*>(m_solver.getMomentumX());
      t_real* l_hvArr = const_cast<t_real*>(m_solver.getMomentumY());
      for (t_idx l_y = 0; l_y < m_ny; l_y++) {
        for (t_idx l_x = 0; l_x < m_nx; l_x++) {
          const t_idx l_i = l_x + l_y * l_stride;
          const t_real l_hc = l_hArr[l_i];
          if (l_hc <= c_dryTolerance)
            continue;
          const t_real l_hu = l_huArr[l_i];
          const t_real l_hv = l_hvArr[l_i];
          const t_real l_speed = std::sqrt(l_hu * l_hu + l_hv * l_hv) / l_hc;
          const t_real l_speedCap = k_maxFroude * std::sqrt(g * l_hc);
          if (l_speed > l_speedCap) {
            const t_real l_f = l_speedCap / l_speed;
            l_huArr[l_i] = l_hu * l_f;
            l_hvArr[l_i] = l_hv * l_f;
          }
        }
      }
    }

    const t_real* l_h = m_solver.getHeight();
    for (t_idx l_y = 0; l_y < m_ny; l_y++)
      std::copy(l_h + l_y * l_stride, l_h + l_y * l_stride + m_nx,
                l_frame.begin() + l_y * m_nx);

    m_buffer.write(l_frame.data(), m_nx * m_ny);

    m_simTimeAccum.store(m_simTimeAccum.load() + l_dt); // single writer

    const double l_comp =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - l_t0)
            .count();
    const double l_prev = m_stepSeconds.load();
    m_stepSeconds.store(l_prev <= 0.0 ? l_comp : 0.9 * l_prev + 0.1 * l_comp);
    m_steps.fetch_add(1);

    const double l_scale = m_timeScale.load();
    if (l_scale > 0.0 && l_dt > 0.0) {
      l_next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(l_dt / l_scale));
      const auto l_now = std::chrono::steady_clock::now();
      if (l_next > l_now)
        std::this_thread::sleep_until(l_next);
      else
        l_next = l_now; // fell behind: resync to avoid spiralling
    }
  }
}

} // namespace visualization
} // namespace tsunami_lab
