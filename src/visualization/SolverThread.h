/**
 * @author Jan Vogt (jan.vogt AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Runs a WavePropagation2d solver on a background thread and publishes each new
 * water-height frame into a SimBuffer for the renderer.
 **/
#ifndef TSUNAMI_LAB_VISUALIZATION_SOLVERTHREAD
#define TSUNAMI_LAB_VISUALIZATION_SOLVERTHREAD

#include "../constants.h"
#include "../patches/WavePropagation2d/WavePropagation2d.h"
#include "SimBuffer.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tsunami_lab {
namespace visualization {

class SolverThread {
public:
  SolverThread(SimBuffer& io_buffer, t_idx i_nx, t_idx i_ny, t_real i_dxy);

  ~SolverThread();

  patches::WavePropagation2d& solver() { return m_solver; }

  // Derives a CFL-stable scaling from the current state and launches the loop.
  // No-op if already running.
  void start();

  // Signals the loop to stop and joins the thread. Safe to call repeatedly.
  void stop();

  bool running() const { return m_running.load(); }

  t_idx steps() const { return m_steps.load(); }

  // Simulated time elapsed.
  double simTime() const { return m_simTimeAccum.load(); }

  t_real scaling() const { return m_scaling.load(); }

  // Simulated seconds to advance per wall-clock second; 0 = uncapped.
  void setTimeScale(double i_simSecondsPerRealSecond) {
    m_timeScale.store(i_simSecondsPerRealSecond);
  }

  // Largest rate the hardware can sustain (smoothed dt / wall-time-per-step).
  // Zero until a few steps have been measured.
  double maxTimeScale() const {
    const double l_step = m_stepSeconds.load();
    if (l_step <= 0.0)
      return 0.0;
    return ((double)m_scaling.load() * (double)m_dxy) / l_step;
  }

  t_idx nx() const { return m_nx; }
  t_idx ny() const { return m_ny; }
  t_real dxy() const { return m_dxy; }

  // Virtual gauges: one time series (sea-surface anomaly eta = h + b) per
  // cell, sampled once per solver step. Index-order is the caller's contract
  // to keep in sync with its own station metadata (name/lon/lat) — this
  // class only knows cell coordinates and recorded values.
  struct StationPoint {
    float time;
    float eta;
  };

  // Registers a gauge at solver-local cell (i_ix, i_iy); appends to the end
  // of the station list. Thread-safe — may be called while run() is active.
  void addStation(t_idx i_ix, t_idx i_iy);
  void clearStations();
  size_t stationCount() const;
  // Copies the recorded series for station i_idx (empty if out of range).
  std::vector<StationPoint> stationSeries(size_t i_idx) const;

private:
  //! Background loop: setGhost → timeStep → publish, until stop().
  void run();

  //! max(|u|, |v|) + sqrt(g*h) over all wet cells of the current state.
  t_real maxWaveSpeed();

  SimBuffer& m_buffer;
  patches::WavePropagation2d m_solver;
  t_idx m_nx;
  t_idx m_ny;
  t_real m_dxy;
  //! CFL-derived scaling for the step about to run; re-derived every step.
  std::atomic<t_real> m_scaling{0};
  std::string m_mode = "fwave";

  std::thread m_thread;
  std::atomic<bool> m_running{false};
  std::atomic<t_idx> m_steps{0};
  std::atomic<double> m_timeScale{0.0};
  //! Smoothed wall-clock seconds per step (excludes pacing sleep).
  std::atomic<double> m_stepSeconds{0.0};
  //! Simulated seconds elapsed, accumulated once per step (see simTime()).
  std::atomic<double> m_simTimeAccum{0.0};

  // Gauges: recorded once per step in run(), read via stationSeries() from
  // the caller's thread — guarded by m_stationsMtx on both sides. Capped and
  // halved (see run()) so a long-running sim can't grow this unboundedly.
  struct StationRec {
    t_idx ix, iy;
    std::vector<StationPoint> samples;
  };
  static constexpr size_t k_maxStationSamples = 4000;
  mutable std::mutex m_stationsMtx;
  std::vector<StationRec> m_stations;
};

} // namespace visualization
} // namespace tsunami_lab

#endif
