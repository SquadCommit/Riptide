// Browser entry point: owns the WebGL2 context, the render loop and the
// application state machine (globe selection → region preview → live sim).
//
// This is the web counterpart of the retired native main_viz.cpp. The UI
// itself lives in JS/React: state flows out through getState() (embind),
// actions flow in through the exported functions at the bottom. Only canvas
// input (drag/zoom/click) is handled here, via the HTML5 event callbacks.

#include "displacement/OkadaDisplacement.h"
#include "displacement/OkadaFactory.h"
#include "displacement/SubductionScaling.h"
#include "displacement/WellsCoppersmith.h"
#include "io/Slab2Reader.h"
#include "visualization/Camera.h"
#include "visualization/Gebco.h"
#include "visualization/GlobeView.h"
#include "visualization/RegionView.h"
#include "visualization/Scenario.h"
#include "visualization/SimBuffer.h"
#include "visualization/SolverThread.h"
#include "web/Slab2Web.h"
#include "web/WebData.h"

#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/html5.h>
#include <emscripten/val.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace gv = tsunami_lab::visualization;
namespace disp = tsunami_lab::displacement;
namespace web = tsunami_lab::web;

// Application state

enum class AppState { REGION_SELECT, REGION_PREVIEW };

static AppState g_state = AppState::REGION_SELECT;

static gv::Camera g_camera;
static std::unique_ptr<gv::GlobeView> g_globeView;
static std::unique_ptr<gv::RegionView> g_regionView;

static std::string g_regionError;
// Max samples per axis when cropping the bathymetry view (0 = whatever the
// source grid provides).
static int g_bathMaxDim = 0;
// Last selection that was successfully loaded (region-view reload + sim).
static gv::BBox g_loadedSel;

// Mouse state (CSS pixels, filled by the HTML5 callbacks)
static bool g_mouseLeft = false;
static bool g_mouseMiddle = false;
static double g_lastX = 0, g_lastY = 0;
static int g_screenW = 1280, g_screenH = 720; // CSS px (unprojection)
static double g_dpr = 1.0;                    // device pixel ratio
// Distinguishes a click (zone suggestion / epicentre) from a drag.
static double g_pressX = 0, g_pressY = 0;
static float g_dragDist = 0.0f;
// Keys currently held (WASD/arrow pan), set by the key callbacks.
static bool g_keyL = false, g_keyR = false, g_keyU = false, g_keyD = false;

// Slab2 subduction geometry (fetched as a pre-converted bundle at boot;
// null until then -> fallback parameters).
static std::unique_ptr<tsunami_lab::io::Slab2Reader> g_slab2;
// When true, an earthquake can only be triggered inside Slab2 coverage.
static bool g_restrictToSlab2 = true;
// Slab2 sample at the current click (or fallback); valid == false -> no
// coverage.
static tsunami_lab::io::Slab2Point g_slabPt = {0.0, 0.0, 0.0, false, nullptr};

// Earthquake source: click location + moment magnitude.
static float g_mw = 8.0f;
static double g_epiLon = 0, g_epiLat = 0;
static float g_epiWorldX = 0.0f;
static float g_epiWorldZ = 0.0f;
static bool g_hasClick = false;

static constexpr double k_fallbackDepth = 20000.0; // 20 km (m)
static constexpr double k_fallbackStrike = 0.0;    // deg
static constexpr double k_fallbackDip = 15.0;      // deg
static constexpr double k_rake = 90.0;             // pure thrust (fixed)

// The displacement currently applied to the preview; null until a click's
// background build finishes (or after clearing).
static std::unique_ptr<disp::OkadaDisplacement> g_displModel;

// Background displacement build (worker thread), same protocol as the native
// app: a click launches the job, pollDisplacementJob() uploads on GL thread.
struct DispResult {
  std::vector<float> disp;
  float peak;
};
static std::future<DispResult> g_dispFuture;
static bool g_dispComputing = false;
static bool g_dispDirty = false;
static std::unique_ptr<disp::OkadaDisplacement> g_pendingModel;

// Live simulation
static std::unique_ptr<gv::SimBuffer> g_simBuf;
static std::unique_ptr<gv::SolverThread> g_sim;
static float g_simCellSize = 1000.0f;
static float g_simSpeed = 120.0f;
static bool g_simAutoSpeed = false;

// Geographic bounds/dims of the grid the *current* g_sim was built over —
// needed to map a station's lon/lat to solver cell indices, both when
// (re)starting a run and when a station is added while one is live.
struct SimGeo {
  double lonMin = 0, lonMax = 0, latMin = 0, latMax = 0;
  tsunami_lab::t_idx nx = 0, ny = 0;
};
static SimGeo g_simGeo;

// Virtual gauges (name/lon/lat); recorded values live in g_sim, index-aligned
// 1:1 with this list (see rebuildSimStations()). Survive across sim restarts.
struct StationDef {
  std::string name;
  double lon, lat;
};
static std::vector<StationDef> g_stations;
// Next canvas click in REGION_PREVIEW places a station instead of moving the
// epicentre; cleared after one placement (see setPlacingStation()).
static bool g_placingStation = false;

static double g_lastFrameTime = 0.0;
static bool g_booted = false;

static bool simRunning() { return g_sim && g_sim->running(); }

// Geometry helpers (unchanged from the native app)

static glm::vec2 regionUnproject(
    float i_mx, float i_my, int i_w, int i_h, const gv::Camera& i_cam) {
  float l_aspect = (i_h > 0) ? (float)i_w / (float)i_h : 1.0f;
  float l_ndcX = (2.0f * i_mx / (float)i_w) - 1.0f;
  float l_ndcY = 1.0f - (2.0f * i_my / (float)i_h);
  glm::mat4 l_invVP = glm::inverse(i_cam.projection(l_aspect) * i_cam.view());
  glm::vec4 l_near = l_invVP * glm::vec4(l_ndcX, l_ndcY, -1.0f, 1.0f);
  glm::vec4 l_far = l_invVP * glm::vec4(l_ndcX, l_ndcY, 1.0f, 1.0f);
  l_near /= l_near.w;
  l_far /= l_far.w;
  glm::vec3 l_dir = glm::normalize(glm::vec3(l_far) - glm::vec3(l_near));
  if (std::abs(l_dir.y) < 1e-6f)
    return {0.0f, 0.0f};
  float l_t = -l_near.y / l_dir.y;
  glm::vec3 l_world = glm::vec3(l_near) + l_t * l_dir;
  return {l_world.x, l_world.z};
}

static float sampleGrid(
    const std::vector<float>& i_a, int i_w, int i_h, double i_fx, double i_fy) {
  i_fx = std::min(std::max(i_fx, 0.0), (double)(i_w - 1));
  i_fy = std::min(std::max(i_fy, 0.0), (double)(i_h - 1));
  int l_x0 = (int)i_fx, l_y0 = (int)i_fy;
  int l_x1 = std::min(l_x0 + 1, i_w - 1), l_y1 = std::min(l_y0 + 1, i_h - 1);
  double l_tx = i_fx - l_x0, l_ty = i_fy - l_y0;
  float l_v00 = i_a[(size_t)l_y0 * i_w + l_x0];
  float l_v01 = i_a[(size_t)l_y0 * i_w + l_x1];
  float l_v10 = i_a[(size_t)l_y1 * i_w + l_x0];
  float l_v11 = i_a[(size_t)l_y1 * i_w + l_x1];
  double l_top = l_v00 * (1 - l_tx) + l_v01 * l_tx;
  double l_bot = l_v10 * (1 - l_tx) + l_v11 * l_tx;
  return (float)(l_top * (1 - l_ty) + l_bot * l_ty);
}

static void regionMetres(double i_lonMin,
                         double i_lonMax,
                         double i_latMin,
                         double i_latMax,
                         double& o_widthM,
                         double& o_heightM) {
  const double l_latC = 0.5 * (i_latMin + i_latMax);
  const double l_mPerLat = 111132.0;
  const double l_mPerLon = 111320.0 * std::cos(l_latC * M_PI / 180.0);
  o_widthM = (i_lonMax - i_lonMin) * l_mPerLon;
  o_heightM = (i_latMax - i_latMin) * l_mPerLat;
}

static void simGridFor(double i_widthM,
                       double i_heightM,
                       float i_cellSize,
                       tsunami_lab::t_idx& o_nx,
                       tsunami_lab::t_idx& o_ny,
                       double& o_dxy) {
  using tsunami_lab::t_idx;
  // Browser tabs get far less memory headroom than a native process; 2048²
  // cells keep the solver arrays near ~130 MB.
  constexpr t_idx k_cap = 2048;
  o_dxy = std::max(1.0, (double)i_cellSize);
  o_nx = std::max<t_idx>(2, (t_idx)std::llround(i_widthM / o_dxy));
  o_ny = std::max<t_idx>(2, (t_idx)std::llround(i_heightM / o_dxy));
  if (o_nx > k_cap || o_ny > k_cap) {
    const double l_f = std::max((double)o_nx / k_cap, (double)o_ny / k_cap);
    o_dxy *= l_f;
    o_nx = std::max<t_idx>(2, (t_idx)std::llround(i_widthM / o_dxy));
    o_ny = std::max<t_idx>(2, (t_idx)std::llround(i_heightM / o_dxy));
  }
}

// Earthquake displacement

// Builds the Okada displacement for the current magnitude and click location.
// With the Slab2 bundle loaded, the factory supplies depth/strike/dip where
// the slab is present. Outside coverage the result depends on
// g_restrictToSlab2: restricted -> nullptr (no fault); unrestricted ->
// fallback orientation/depth with Wells & Coppersmith geometry.
static std::unique_ptr<disp::OkadaDisplacement> buildDisplacementModel() {
  if (g_slab2) {
    std::unique_ptr<disp::OkadaDisplacement> l_model =
        disp::OkadaFactory::fromMagnitudeAndLocation(g_mw, g_epiLon, g_epiLat,
                                                     *g_slab2, k_rake);
    if (l_model)
      return l_model; // inside Slab2 coverage
    if (g_restrictToSlab2)
      return nullptr; // outside coverage and restricted
  }

  disp::WellsCoppersmith::FaultGeometry l_geo =
      disp::WellsCoppersmith::fromMagnitude(g_mw);
  return std::unique_ptr<disp::OkadaDisplacement>(new disp::OkadaDisplacement(
      k_fallbackStrike, k_fallbackDip, k_rake, l_geo.slip, l_geo.length,
      l_geo.width, k_fallbackDepth));
}

static void startDisplacementJob() {
  if (!g_hasClick || !g_regionView || g_dispComputing)
    return;
  std::unique_ptr<disp::OkadaDisplacement> l_model = buildDisplacementModel();
  if (!l_model)
    return; // outside Slab2 coverage (restricted)
  g_pendingModel = std::move(l_model);
  g_dispComputing = true;
  g_dispDirty = false;

  const gv::RegionView* l_view = g_regionView.get();
  const disp::OkadaDisplacement* l_m = g_pendingModel.get();
  const float l_x = g_epiWorldX, l_z = g_epiWorldZ;
  g_dispFuture = std::async(std::launch::async, [l_view, l_m, l_x, l_z]() {
    DispResult l_r;
    l_view->computeDisplacementField(l_x, l_z, *l_m, l_r.disp, l_r.peak);
    return l_r;
  });
}

static void pollDisplacementJob() {
  if (!g_dispComputing || !g_dispFuture.valid())
    return;
  if (g_dispFuture.wait_for(std::chrono::seconds(0)) !=
      std::future_status::ready)
    return;
  DispResult l_r = g_dispFuture.get();
  g_dispComputing = false;
  if (g_regionView)
    g_regionView->applyDisplacementField(l_r.disp, l_r.peak);
  g_displModel = std::move(g_pendingModel);
  if (g_dispDirty)
    startDisplacementJob(); // magnitude changed mid-build — redo once
}

static void onRegionClick(float i_mx, float i_my) {
  if (!g_regionView || !g_regionView->loaded())
    return;
  if (g_dispComputing)
    return; // previous click's displacement is still building — ignore
  glm::vec2 l_world =
      regionUnproject(i_mx, i_my, g_screenW, g_screenH, g_camera);
  g_epiWorldX = l_world.x;
  g_epiWorldZ = l_world.y;
  g_regionView->worldToLonLat(l_world.x, l_world.y, g_epiLon, g_epiLat);

  if (g_slab2)
    g_slabPt = g_slab2->query(g_epiLon, g_epiLat);
  else
    g_slabPt = {k_fallbackDepth, k_fallbackStrike, k_fallbackDip, true,
                nullptr};
  g_hasClick = true;

  g_displModel.reset();
  g_regionView->clearDisplacement();
  startDisplacementJob();
}

// Selection rectangle proposed when the user clicks a subduction zone on the
// world map: centred on the click, sized for a typical tsunami run and
// clamped to the world bounds and the per-axis selection limit.
static gv::BBox
suggestSlabSelection(double i_lon, double i_lat, float i_maxSelDeg) {
  const double l_lonSpan = std::min(12.0, (double)i_maxSelDeg);
  const double l_latSpan = std::min(8.0, (double)i_maxSelDeg);
  double l_lonMin = i_lon - 0.5 * l_lonSpan;
  double l_latMin = i_lat - 0.5 * l_latSpan;
  l_lonMin = std::min(std::max(l_lonMin, -180.0), 180.0 - l_lonSpan);
  l_latMin = std::min(std::max(l_latMin, -90.0), 90.0 - l_latSpan);
  gv::BBox l_b;
  l_b.lonMin = (float)l_lonMin;
  l_b.lonMax = (float)(l_lonMin + l_lonSpan);
  l_b.latMin = (float)l_latMin;
  l_b.latMax = (float)(l_latMin + l_latSpan);
  return l_b;
}

// Live simulation setup (identical maths to the native app; the bathymetry
// crop comes from the in-memory web grids via gebco::readRegion)

static bool buildSimSetup(tsunami_lab::t_idx& o_nx,
                          tsunami_lab::t_idx& o_ny,
                          float& o_dxy,
                          std::vector<float>& o_bath,
                          std::vector<float>& o_height,
                          double& o_lonMin,
                          double& o_lonMax,
                          double& o_latMin,
                          double& o_latMax) {
  using namespace tsunami_lab;

  if (!g_loadedSel.valid())
    return false;

  gv::gebco::Region l_src;
  if (!gv::gebco::readRegion("web", g_loadedSel, l_src, 2048) || l_src.w < 2 ||
      l_src.h < 2)
    return false;
  o_lonMin = l_src.lonMin;
  o_lonMax = l_src.lonMax;
  o_latMin = l_src.latMin;
  o_latMax = l_src.latMax;

  const double l_latC = 0.5 * (l_src.latMin + l_src.latMax);
  const double l_mPerLat = 111132.0;
  const double l_mPerLon = 111320.0 * std::cos(l_latC * M_PI / 180.0);
  const double l_srcDLonM =
      (l_src.w > 1)
          ? (l_src.lonMax - l_src.lonMin) / (double)(l_src.w - 1) * l_mPerLon
          : 0.0;
  const double l_srcDLatM =
      (l_src.h > 1)
          ? (l_src.latMax - l_src.latMin) / (double)(l_src.h - 1) * l_mPerLat
          : 0.0;
  double l_widthM = 0.0, l_heightM = 0.0;
  regionMetres(l_src.lonMin, l_src.lonMax, l_src.latMin, l_src.latMax, l_widthM,
               l_heightM);
  if (l_widthM <= 0.0 || l_heightM <= 0.0)
    return false;

  double l_dxy = 0.0;
  t_idx l_nx = 0, l_ny = 0;
  simGridFor(l_widthM, l_heightM, g_simCellSize, l_nx, l_ny, l_dxy);

  o_nx = l_nx;
  o_ny = l_ny;
  o_dxy = (float)l_dxy;
  o_bath.assign((size_t)l_nx * l_ny, 0.0f);
  o_height.assign((size_t)l_nx * l_ny, 0.0f);

  const bool l_hasDisp =
      g_displModel && g_regionView && g_regionView->hasDisplacement();

  for (t_idx l_j = 0; l_j < l_ny; l_j++) {
    const double l_lat = l_src.latMin + (double)l_j *
                                            (l_src.latMax - l_src.latMin) /
                                            (double)(l_ny - 1);
    const double l_fy = (l_lat - l_src.latMin) / (l_src.latMax - l_src.latMin) *
                        (double)(l_src.h - 1);
    for (t_idx l_i = 0; l_i < l_nx; l_i++) {
      const double l_lon = l_src.lonMin + (double)l_i *
                                              (l_src.lonMax - l_src.lonMin) /
                                              (double)(l_nx - 1);
      const double l_fx = (l_lon - l_src.lonMin) /
                          (l_src.lonMax - l_src.lonMin) * (double)(l_src.w - 1);

      const float l_b = sampleGrid(l_src.elev, l_src.w, l_src.h, l_fx, l_fy);
      const size_t l_k = (size_t)l_j * l_nx + l_i;
      o_bath[l_k] = l_b;

      // still water to sea level; dry below the tolerance
      float l_h = (l_b < -tsunami_lab::c_dryTolerance) ? -l_b : 0.0f;
      if (l_hasDisp && l_b < 0.0f) {
        const double l_east = (l_lon - g_epiLon) * l_mPerLon;
        const double l_north = (l_lat - g_epiLat) * l_mPerLat;
        double l_uz = g_displModel->verticalDisplacement(l_east, l_north);

        // Tanioka & Satake (1996): fold horizontal seafloor motion over the
        // local bathymetric slope into the effective vertical displacement.
        double l_uEast = 0.0, l_uNorth = 0.0;
        g_displModel->horizontalDisplacement(l_east, l_north, l_uEast,
                                             l_uNorth);
        if ((l_uEast != 0.0 || l_uNorth != 0.0) && l_srcDLonM > 0.0 &&
            l_srcDLatM > 0.0) {
          const float l_eE =
              sampleGrid(l_src.elev, l_src.w, l_src.h, l_fx + 1.0, l_fy);
          const float l_eW =
              sampleGrid(l_src.elev, l_src.w, l_src.h, l_fx - 1.0, l_fy);
          const float l_eN =
              sampleGrid(l_src.elev, l_src.w, l_src.h, l_fx, l_fy + 1.0);
          const float l_eS =
              sampleGrid(l_src.elev, l_src.w, l_src.h, l_fx, l_fy - 1.0);
          const double l_gradEast = (l_eE - l_eW) / (2.0 * l_srcDLonM);
          const double l_gradNorth = (l_eN - l_eS) / (2.0 * l_srcDLatM);
          l_uz = disp::effectiveVerticalDisplacement(l_uz, l_uEast, l_uNorth,
                                                     l_gradEast, l_gradNorth);
        }

        l_h += (float)l_uz;
        if (l_h < 0.0f)
          l_h = 0.0f;
      }
      o_height[l_k] = l_h;
    }
  }
  return true;
}

// Maps a lon/lat to the nearest cell of the *current* g_sim grid (g_simGeo),
// clamped to the grid — a station is always placed by clicking the loaded
// terrain, so it is expected to fall inside, but clamping keeps this total
// (no invalid-index case to reject) rather than dropping the station.
static void lonLatToStationCell(double i_lon,
                                double i_lat,
                                tsunami_lab::t_idx& o_ix,
                                tsunami_lab::t_idx& o_iy) {
  using namespace tsunami_lab;
  double l_fx = (g_simGeo.lonMax > g_simGeo.lonMin)
                    ? (i_lon - g_simGeo.lonMin) /
                          (g_simGeo.lonMax - g_simGeo.lonMin) *
                          (double)(g_simGeo.nx - 1)
                    : 0.0;
  double l_fy = (g_simGeo.latMax > g_simGeo.latMin)
                    ? (i_lat - g_simGeo.latMin) /
                          (g_simGeo.latMax - g_simGeo.latMin) *
                          (double)(g_simGeo.ny - 1)
                    : 0.0;
  l_fx = std::min(std::max(l_fx, 0.0), (double)(g_simGeo.nx - 1));
  l_fy = std::min(std::max(l_fy, 0.0), (double)(g_simGeo.ny - 1));
  o_ix = (t_idx)std::lround(l_fx);
  o_iy = (t_idx)std::lround(l_fy);
}

// Re-registers every station with g_sim in g_stations order, so indices stay
// 1:1 aligned (see StationDef comment). Used after removal — the trade-off
// is that removing one station resets every other station's recorded
// history too, which keeps this simple; adding a station never does.
static void rebuildSimStations() {
  if (!g_sim)
    return;
  g_sim->clearStations();
  for (const StationDef& l_st : g_stations) {
    tsunami_lab::t_idx l_ix, l_iy;
    lonLatToStationCell(l_st.lon, l_st.lat, l_ix, l_iy);
    g_sim->addStation(l_ix, l_iy);
  }
}

// Adds a station at (i_lon, i_lat); auto-names it if i_name is empty.
// Registers immediately with a live g_sim (keeping the 1:1 index alignment);
// otherwise the station is picked up the next time startSimulation() runs.
// Returns the new station's index.
static int addStationAtLonLat(std::string i_name, double i_lon, double i_lat) {
  if (i_name.empty())
    i_name = "Station " + std::to_string(g_stations.size() + 1);
  g_stations.push_back({std::move(i_name), i_lon, i_lat});
  if (g_sim && g_simGeo.nx >= 2 && g_simGeo.ny >= 2) {
    tsunami_lab::t_idx l_ix, l_iy;
    lonLatToStationCell(i_lon, i_lat, l_ix, l_iy);
    g_sim->addStation(l_ix, l_iy);
  }
  return (int)g_stations.size() - 1;
}

static void placeStationAtScreen(float i_mx, float i_my) {
  if (!g_regionView || !g_regionView->loaded())
    return;
  const glm::vec2 l_world =
      regionUnproject(i_mx, i_my, g_screenW, g_screenH, g_camera);
  double l_lon = 0.0, l_lat = 0.0;
  g_regionView->worldToLonLat(l_world.x, l_world.y, l_lon, l_lat);
  addStationAtLonLat("", l_lon, l_lat);
}

static void stopSimulation() {
  if (g_sim) {
    g_sim->stop();
    g_sim.reset();
  }
  g_simBuf.reset();
  if (g_regionView)
    g_regionView->endSimulation();
}

static void startSimulation() {
  using namespace tsunami_lab;

  stopSimulation();

  t_idx l_nx = 0, l_ny = 0;
  float l_dxy = 0.0f;
  std::vector<float> l_bath, l_height;
  double l_lonMin = 0, l_lonMax = 0, l_latMin = 0, l_latMax = 0;
  if (!buildSimSetup(l_nx, l_ny, l_dxy, l_bath, l_height, l_lonMin, l_lonMax,
                     l_latMin, l_latMax)) {
    g_regionError = "Simulation: Bathymetrie-Setup fehlgeschlagen.";
    return;
  }

  g_simBuf.reset(new gv::SimBuffer(l_nx, l_ny));
  g_sim.reset(new gv::SolverThread(*g_simBuf, l_nx, l_ny, l_dxy));
  g_simGeo = {l_lonMin, l_lonMax, l_latMin, l_latMax, l_nx, l_ny};

  patches::WavePropagation2d& l_solver = g_sim->solver();
  for (t_idx l_j = 0; l_j < l_ny; l_j++)
    for (t_idx l_i = 0; l_i < l_nx; l_i++) {
      const size_t l_k = (size_t)l_j * l_nx + l_i;
      l_solver.setBathymetry(l_i, l_j, l_bath[l_k]);
      l_solver.setHeight(l_i, l_j, l_height[l_k]);
      l_solver.setMomentumX(l_i, l_j, 0.0f);
      l_solver.setMomentumY(l_i, l_j, 0.0f);
    }

  if (g_regionView)
    g_regionView->beginSimulation(l_nx, l_ny, l_bath.data());
  rebuildSimStations();
  g_sim->setTimeScale(g_simSpeed);
  g_sim->start();
}

// Camera helpers

static void setCameraGlobeView(gv::Camera& cam) {
  cam.setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
  cam.setAzimuth(0.0f);
  // A 3/4 oblique angle so the sphere reads as a globe (curved limb, shaded
  // terminator) rather than a top-down disc.
  cam.setElevation(0.4f);
  // ~2.6x the globe radius keeps it filling most of the 45 deg-FOV frame.
  cam.setDistance(150.0f);
}

static void setCameraRegionView(gv::Camera& cam) {
  cam.setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
  cam.setAzimuth(0.0f);
  cam.setElevation(0.7f);
  cam.setDistance(330.0f);
}

// Region loading

static bool loadRegionAndEnterPreview(const gv::BBox& i_sel) {
  g_regionError.clear();
  gv::gebco::Region l_reg;
  if (!gv::gebco::readRegion("web", i_sel, l_reg, g_bathMaxDim) ||
      !g_regionView || !g_regionView->load(l_reg)) {
    g_regionError = "Laden der Bathymetrie fehlgeschlagen.";
    return false;
  }
  g_loadedSel = i_sel;
  g_state = AppState::REGION_PREVIEW;
  if (g_slab2)
    g_regionView->buildSlab2Overlay(*g_slab2);
  setCameraRegionView(g_camera);
  return true;
}

// One-click historical scenario; the region grid bytes have already been
// handed over via loadScenarioBytes() when this runs.
static void triggerScenario(int i_idx) {
  if (i_idx < 0 || i_idx >= gv::k_numScenarios || g_dispComputing ||
      !g_regionView)
    return;
  const gv::Scenario& l_sc = gv::k_scenarios[i_idx];
  if (g_globeView)
    g_globeView->setSelection(l_sc.region);
  if (!loadRegionAndEnterPreview(l_sc.region))
    return;

  g_mw = l_sc.magnitude;
  g_epiLon = l_sc.epiLon;
  g_epiLat = l_sc.epiLat;
  g_regionView->lonLatToWorld(g_epiLon, g_epiLat, g_epiWorldX, g_epiWorldZ);

  if (g_slab2)
    g_slabPt = g_slab2->query(g_epiLon, g_epiLat);
  else
    g_slabPt = {k_fallbackDepth, k_fallbackStrike, k_fallbackDip, true,
                nullptr};
  g_hasClick = true;

  g_displModel.reset();
  g_regionView->clearDisplacement();
  startDisplacementJob();
}

// Canvas input (HTML5 events; coordinates are CSS pixels on the canvas)

static EM_BOOL onMouseDown(int, const EmscriptenMouseEvent* i_e, void*) {
  g_lastX = i_e->targetX;
  g_lastY = i_e->targetY;
  if (i_e->button == 0) {
    g_mouseLeft = true;
    g_pressX = g_lastX;
    g_pressY = g_lastY;
    g_dragDist = 0.0f;
    if (g_state == AppState::REGION_SELECT && g_globeView)
      g_globeView->onMousePress((float)g_lastX, (float)g_lastY, g_screenW,
                                g_screenH, g_camera);
  }
  if (i_e->button == 1)
    g_mouseMiddle = true;
  return EM_TRUE;
}

static EM_BOOL onMouseUp(int, const EmscriptenMouseEvent* i_e, void*) {
  // Registered on the document so drags ending off-canvas still release, but
  // only presses that STARTED on the canvas count — otherwise every panel
  // button click would land as a stale-coordinate canvas click (and, in the
  // region view, silently move the epicentre).
  if (i_e->button == 0) {
    if (!g_mouseLeft)
      return EM_FALSE;
    g_mouseLeft = false;
    if (g_state == AppState::REGION_SELECT && g_globeView) {
      g_globeView->onMouseRelease();
      // Released without dragging on a subduction zone: propose a selection
      // rectangle around that spot as a quick start for a simulation region.
      const bool l_isClick =
          std::abs(g_lastX - g_pressX) + std::abs(g_lastY - g_pressY) < 5.0;
      if (l_isClick && g_slab2 && g_globeView->showSlab2Overlay) {
        float l_lon, l_lat;
        if (g_globeView->screenToLonLat((float)g_lastX, (float)g_lastY,
                                        g_screenW, g_screenH, g_camera, l_lon,
                                        l_lat) &&
            g_slab2->query(l_lon, l_lat).valid)
          g_globeView->setSelection(
              suggestSlabSelection(l_lon, l_lat, g_globeView->maxSelDeg));
      }
    } else if (g_state == AppState::REGION_PREVIEW && g_dragDist < 5.0f) {
      if (g_placingStation) {
        // Station placement is allowed while a sim is running (watch a new
        // gauge from here on); the epicentre below is not.
        placeStationAtScreen((float)g_lastX, (float)g_lastY);
        g_placingStation = false;
      } else if (!simRunning()) {
        // Released without dragging: treat as a click and pick an epicentre.
        onRegionClick((float)g_lastX, (float)g_lastY);
      }
    }
  }
  if (i_e->button == 1)
    g_mouseMiddle = false;
  return EM_TRUE;
}

static EM_BOOL onMouseMove(int, const EmscriptenMouseEvent* i_e, void*) {
  float dx = (float)(i_e->targetX - g_lastX);
  float dy = (float)(i_e->targetY - g_lastY);
  g_lastX = i_e->targetX;
  g_lastY = i_e->targetY;

  if (g_state == AppState::REGION_SELECT) {
    if (g_mouseLeft && g_globeView)
      g_globeView->onMouseMove((float)g_lastX, (float)g_lastY, g_screenW,
                               g_screenH, g_camera);
    // Rotate the globe instead of panning an (now nonexistent) flat map.
    if (g_mouseMiddle)
      g_camera.onMouseDrag(dx, dy);
  } else {
    if (g_mouseLeft) {
      g_dragDist += std::abs(dx) + std::abs(dy);
      g_camera.onMouseDrag(dx, dy);
    }
    if (g_mouseMiddle)
      g_camera.onMiddleDrag(dx, dy);
  }
  return EM_TRUE;
}

static EM_BOOL onWheel(int, const EmscriptenWheelEvent* i_e, void*) {
  // Normalise to GLFW-like scroll steps: one wheel notch ≈ 1.0.
  double l_dy = i_e->deltaY;
  if (i_e->deltaMode == DOM_DELTA_LINE)
    l_dy *= 40.0;
  g_camera.onScroll((float)(-l_dy / 100.0));
  return EM_TRUE; // prevent page scroll
}

static bool matchKey(const EmscriptenKeyboardEvent* i_e, const char* i_code) {
  return std::strcmp(i_e->code, i_code) == 0;
}

static EM_BOOL onKey(int i_type, const EmscriptenKeyboardEvent* i_e, void*) {
  const bool l_down = (i_type == EMSCRIPTEN_EVENT_KEYDOWN);
  if (matchKey(i_e, "KeyA") || matchKey(i_e, "ArrowLeft"))
    g_keyL = l_down;
  else if (matchKey(i_e, "KeyD") || matchKey(i_e, "ArrowRight"))
    g_keyR = l_down;
  else if (matchKey(i_e, "KeyW") || matchKey(i_e, "ArrowUp"))
    g_keyU = l_down;
  else if (matchKey(i_e, "KeyS") || matchKey(i_e, "ArrowDown"))
    g_keyD = l_down;
  else
    return EM_FALSE;
  // Only consume the keys handled here; everything else stays with the page.
  return EM_TRUE;
}

// Render loop — called once per animation frame from JS (requestAnimationFrame
// in main.js); JS owns the loop so the frontend can pause or throttle it.

static void frame() {
  if (!g_booted)
    return;
  const double l_now = emscripten_get_now() / 1000.0;
  float l_dt = (float)(l_now - g_lastFrameTime);
  g_lastFrameTime = l_now;
  l_dt = std::min(l_dt, 0.1f); // background tabs: avoid huge catch-up pans

  // Keyboard pan/rotate
  {
    float l_speed = 200.0f * l_dt;
    float l_kx = (g_keyR ? l_speed : 0.0f) - (g_keyL ? l_speed : 0.0f);
    float l_ky = (g_keyD ? l_speed : 0.0f) - (g_keyU ? l_speed : 0.0f);
    if (l_kx != 0.0f || l_ky != 0.0f) {
      // REGION_SELECT shows the globe: rotate it instead of panning a
      // (now nonexistent) flat map.
      if (g_state == AppState::REGION_SELECT)
        g_camera.onMouseDrag(-l_kx, -l_ky);
      else
        g_camera.onMapPan(-l_kx, -l_ky);
    }
  }

  const int l_fbW = (int)(g_screenW * g_dpr);
  const int l_fbH = (int)(g_screenH * g_dpr);
  glViewport(0, 0, l_fbW, l_fbH);
  glClearColor(0.06f, 0.09f, 0.14f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  float l_aspect = (g_screenH > 0) ? (float)g_screenW / (float)g_screenH : 1.0f;
  glm::mat4 l_vp = g_camera.projection(l_aspect) * g_camera.view();

  pollDisplacementJob();

  if (simRunning()) {
    if (g_simAutoSpeed)
      g_sim->setTimeScale(g_sim->maxTimeScale());
    if (g_simBuf->swap())
      g_regionView->updateWater(g_simBuf->front());
  }

  if (g_state == AppState::REGION_SELECT) {
    if (g_globeView) {
      g_globeView->lodCamDistance = g_camera.getDistance();
      g_globeView->lodViewportPx = l_fbH;
      g_globeView->draw(l_vp);
    }
  } else if (g_regionView) {
    g_regionView->lodViewportPx = l_fbH;
    g_regionView->draw(l_vp, g_camera.position());
  }
}

// Boot + JS API (embind)

static bool boot(uintptr_t i_globePtr,
                 int i_globeSize,
                 int i_cssW,
                 int i_cssH,
                 double i_dpr) {
  if (g_booted)
    return true;

  web::Grid l_grid;
  if (!web::parseGrid((const uint8_t*)i_globePtr, (size_t)i_globeSize,
                      l_grid)) {
    std::fprintf(stderr, "boot: Welt-Gitter unlesbar\n");
    return false;
  }
  web::setGlobeGrid(std::move(l_grid));

  g_screenW = i_cssW;
  g_screenH = i_cssH;
  g_dpr = i_dpr;

  EmscriptenWebGLContextAttributes l_attr;
  emscripten_webgl_init_context_attributes(&l_attr);
  l_attr.majorVersion = 2;
  l_attr.minorVersion = 0;
  l_attr.antialias = EM_TRUE;
  l_attr.depth = EM_TRUE;
  l_attr.alpha = EM_FALSE;
  l_attr.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE l_ctx =
      emscripten_webgl_create_context("#canvas", &l_attr);
  if (l_ctx <= 0) {
    std::fprintf(stderr, "boot: WebGL2-Kontext fehlgeschlagen (%ld)\n",
                 (long)l_ctx);
    return false;
  }
  emscripten_webgl_make_context_current(l_ctx);
  emscripten_set_canvas_element_size("#canvas", (int)(i_cssW * i_dpr),
                                     (int)(i_cssH * i_dpr));

  std::printf("GL: %s\n", (const char*)glGetString(GL_VERSION));
  glEnable(GL_DEPTH_TEST);

  g_globeView.reset(new gv::GlobeView());
  g_regionView.reset(new gv::RegionView());
  // The web globe grid is pre-extracted at its final resolution; the view
  // crops/strides from it via gebco::readRegion ("web" is a dummy path).
  g_globeView->init("web");
  g_regionView->init();
  setCameraGlobeView(g_camera);

  emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE, onMouseDown);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr,
                                  EM_TRUE, onMouseUp);
  emscripten_set_mousemove_callback("#canvas", nullptr, EM_TRUE, onMouseMove);
  emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, onWheel);
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                  EM_FALSE, onKey);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                EM_FALSE, onKey);

  g_lastFrameTime = emscripten_get_now() / 1000.0;
  g_booted = true;
  return true;
}

static void resize(int i_cssW, int i_cssH, double i_dpr) {
  g_screenW = i_cssW;
  g_screenH = i_cssH;
  g_dpr = i_dpr;
  if (g_booted)
    emscripten_set_canvas_element_size("#canvas", (int)(i_cssW * i_dpr),
                                       (int)(i_cssH * i_dpr));
}

// Slab2 bundle fetched (and gunzipped) by JS at boot: enables the
// subduction-zone overlays and real fault geometry at the epicentre.
static bool loadSlab2Bytes(uintptr_t i_ptr, int i_size) {
  std::unique_ptr<tsunami_lab::io::Slab2Reader> l_reader =
      web::parseSlab2((const uint8_t*)i_ptr, (size_t)i_size);
  if (!l_reader) {
    std::fprintf(stderr, "Slab2: Bundle unlesbar\n");
    return false;
  }
  g_slab2 = std::move(l_reader);
  if (g_globeView)
    g_globeView->buildSlab2Overlay(*g_slab2);
  if (g_regionView && g_regionView->loaded())
    g_regionView->buildSlab2Overlay(*g_slab2);
  return true;
}

static void setRestrictToSlab2(bool i_v) { g_restrictToSlab2 = i_v; }

static void setShowSlabOverlay(bool i_v) {
  if (g_globeView)
    g_globeView->showSlab2Overlay = i_v;
  if (g_regionView)
    g_regionView->showSlab2Overlay = i_v;
}

// Everything the frontend needs to render a cursor tooltip / feedback ring
// at CSS position (i_x, i_y): geographic coordinates plus the Slab2 sample.
static emscripten::val getHoverInfo(double i_x, double i_y) {
  using emscripten::val;
  val o = val::object();
  o.set("valid", false);
  double l_lon = 0.0, l_lat = 0.0;
  if (g_state == AppState::REGION_SELECT) {
    float l_flon, l_flat;
    if (!g_globeView ||
        !g_globeView->screenToLonLat((float)i_x, (float)i_y, g_screenW,
                                     g_screenH, g_camera, l_flon, l_flat))
      return o; // cursor is off the visible limb of the globe
    l_lon = l_flon;
    l_lat = l_flat;
  } else {
    if (!g_regionView || !g_regionView->loaded())
      return o;
    const glm::vec2 l_w = regionUnproject((float)i_x, (float)i_y, g_screenW,
                                          g_screenH, g_camera);
    g_regionView->worldToLonLat(l_w.x, l_w.y, l_lon, l_lat);
    if (l_lon < g_regionView->lonMin || l_lon > g_regionView->lonMax ||
        l_lat < g_regionView->latMin || l_lat > g_regionView->latMax)
      return o;
  }
  o.set("valid", true);
  o.set("lon", l_lon);
  o.set("lat", l_lat);
  if (g_slab2) {
    const tsunami_lab::io::Slab2Point l_p = g_slab2->query(l_lon, l_lat);
    val l_s = val::object();
    l_s.set("valid", l_p.valid);
    if (l_p.valid) {
      l_s.set("depth", l_p.depth);
      l_s.set("strike", l_p.strike);
      l_s.set("dip", l_p.dip);
      l_s.set("region", l_p.region ? std::string(l_p.region) : std::string());
    }
    o.set("slab", l_s);
  }
  return o;
}

// Region-grid bytes fetched by JS for scenario i_idx; parses, stores and
// triggers the scenario (region load + epicentre + displacement build).
static bool loadScenarioBytes(int i_idx, uintptr_t i_ptr, int i_size) {
  web::Grid l_grid;
  if (!web::parseGrid((const uint8_t*)i_ptr, (size_t)i_size, l_grid)) {
    g_regionError = "Szenario-Daten unlesbar.";
    return false;
  }
  web::setRegionGrid(std::move(l_grid));
  triggerScenario(i_idx);
  return g_state == AppState::REGION_PREVIEW;
}

// Manual selection: crops from whatever grid covers it — the stitched tile
// grid set up by loadSelectionTiles() below when available, else the coarse
// globe grid as a fallback (see web::pickSource in WebData.cpp).
static bool loadSelection() {
  if (!g_globeView || !g_globeView->hasSelection())
    return false;
  return loadRegionAndEnterPreview(g_globeView->getSelection());
}

// Tile bytes fetched by JS for the current free-hand selection (one tile per
// world cell overlapping the bbox — see tools/make_web_data.py's TILE_DEG
// and tileUrlsForBbox() in web/src/lib/tsunami.ts). i_tiles is a JS array of
// {ptr, size} pairs already copied into the wasm heap; stitches them into
// one region grid and loads it exactly like loadSelection() above, just at
// tile resolution instead of the coarse whole-world fallback.
static bool loadSelectionTiles(emscripten::val i_tiles) {
  const int l_n = i_tiles["length"].as<int>();
  std::vector<web::Grid> l_grids;
  l_grids.reserve((size_t)l_n);
  for (int l_i = 0; l_i < l_n; l_i++) {
    const emscripten::val l_t = i_tiles[l_i];
    const auto l_ptr = (const uint8_t*)l_t["ptr"].as<uintptr_t>();
    const int l_size = l_t["size"].as<int>();
    web::Grid l_g;
    if (web::parseGrid(l_ptr, (size_t)l_size, l_g))
      l_grids.push_back(std::move(l_g));
  }
  web::Grid l_combined;
  if (!web::stitchGrids(l_grids, l_combined)) {
    g_regionError = "Kachel-Daten unlesbar.";
    return false;
  }
  web::setRegionGrid(std::move(l_combined));
  return loadSelection();
}

static void backToGlobe() {
  if (g_dispComputing)
    return; // displacement worker still reads the current grid
  stopSimulation();
  // Stations are anchored to the region just left (lon/lat that may not
  // even fall inside whatever gets loaded next) — drop them along with the
  // sim state rather than carrying stale gauges into an unrelated region.
  g_stations.clear();
  g_placingStation = false;
  g_state = AppState::REGION_SELECT;
  setCameraGlobeView(g_camera);
}

static void reloadRegion(int i_maxDim) {
  if (g_dispComputing || !g_loadedSel.valid())
    return;
  g_bathMaxDim = i_maxDim;
  gv::gebco::Region l_reg;
  if (gv::gebco::readRegion("web", g_loadedSel, l_reg, g_bathMaxDim) &&
      g_regionView->load(l_reg)) {
    g_regionView->clearDisplacement();
    g_displModel.reset();
  } else {
    g_regionError = "Neu laden fehlgeschlagen.";
  }
}

static void setSelection(double i_lonMin,
                         double i_lonMax,
                         double i_latMin,
                         double i_latMax) {
  if (!g_globeView)
    return;
  gv::BBox l_b;
  l_b.lonMin = (float)std::max(i_lonMin, -180.0);
  l_b.lonMax = (float)std::min(i_lonMax, 180.0);
  l_b.latMin = (float)std::max(i_latMin, -90.0);
  l_b.latMax = (float)std::min(i_latMax, 90.0);
  if (l_b.valid())
    g_globeView->setSelection(l_b);
}

static void clearSelection() {
  if (g_globeView)
    g_globeView->clearSelection();
}

static void setMw(float i_mw) { g_mw = i_mw; }

// Slider release: rebuild the displacement for the new magnitude.
static void commitMw() {
  if (!g_hasClick || simRunning())
    return;
  if (g_dispComputing)
    g_dispDirty = true;
  else
    startDisplacementJob();
}

static void clearQuake() {
  if (!g_regionView)
    return;
  g_regionView->clearDisplacement();
  g_displModel.reset();
}

// Virtual gauges (stations)

// Arms/disarms "next canvas click places a station" (see onMouseUp).
static void setPlacingStation(bool i_v) { g_placingStation = i_v; }

// Adds a station directly at (i_lon, i_lat) — used by the panel's own "add
// at current epicentre" shortcut, distinct from click-to-place on the map.
static int addStation(double i_lon, double i_lat) {
  return addStationAtLonLat("", i_lon, i_lat);
}

static void renameStation(int i_idx, std::string i_name) {
  if (i_idx >= 0 && (size_t)i_idx < g_stations.size() && !i_name.empty())
    g_stations[(size_t)i_idx].name = std::move(i_name);
}

static void removeStation(int i_idx) {
  if (i_idx < 0 || (size_t)i_idx >= g_stations.size())
    return;
  g_stations.erase(g_stations.begin() + i_idx);
  rebuildSimStations();
}

static void clearStations() {
  g_stations.clear();
  if (g_sim)
    g_sim->clearStations();
}

// All stations with their full recorded series so far — polled by the panel
// at a slower cadence than getState() (see useTsunami.ts), since this
// serializes every sample of every gauge each call.
static emscripten::val getStations() {
  using emscripten::val;
  val l_arr = val::array();
  for (size_t l_i = 0; l_i < g_stations.size(); l_i++) {
    val l_o = val::object();
    l_o.set("name", g_stations[l_i].name);
    l_o.set("lon", g_stations[l_i].lon);
    l_o.set("lat", g_stations[l_i].lat);
    val l_series = val::array();
    if (g_sim) {
      const auto l_pts = g_sim->stationSeries(l_i);
      for (size_t l_k = 0; l_k < l_pts.size(); l_k++) {
        val l_pt = val::object();
        l_pt.set("t", (double)l_pts[l_k].time);
        l_pt.set("eta", (double)l_pts[l_k].eta);
        l_series.set((unsigned)l_k, l_pt);
      }
    }
    l_o.set("series", l_series);
    l_arr.set((unsigned)l_i, l_o);
  }
  return l_arr;
}

// Pushes the JS-computed colour (see stationColor() in
// web/src/lib/stationColor.ts, the single source of truth so the chart
// legend and the 3d marker always agree) for every station's 3d marker.
// i_markers: array of {lon, lat, r, g, b} (r/g/b in 0..1) — self-contained,
// not index-matched against g_stations, so it can't desync with it.
static void setStationMarkers(emscripten::val i_markers) {
  if (!g_regionView)
    return;
  std::vector<gv::RegionView::StationMarker> l_markers;
  const int l_n = i_markers["length"].as<int>();
  l_markers.reserve((size_t)l_n);
  for (int l_i = 0; l_i < l_n; l_i++) {
    const emscripten::val l_m = i_markers[l_i];
    float l_wx = 0.0f, l_wz = 0.0f;
    g_regionView->lonLatToWorld(l_m["lon"].as<double>(),
                                l_m["lat"].as<double>(), l_wx, l_wz);
    l_markers.push_back({l_wx, l_wz, l_m["r"].as<float>(),
                         l_m["g"].as<float>(), l_m["b"].as<float>()});
  }
  g_regionView->setStationMarkers(l_markers);
}

static void setField(int i_field) {
  if (g_regionView)
    g_regionView->field = (i_field == 1) ? gv::RegionView::Field::Displacement
                                         : gv::RegionView::Field::Bathymetry;
}

static void setVertExaggeration(float i_v) {
  if (g_regionView)
    g_regionView->vertExaggeration = i_v;
}

static void setWaveExaggeration(float i_v) {
  if (g_regionView)
    g_regionView->waveExaggeration = i_v;
}

static void setShowSea(bool i_v) {
  if (g_regionView)
    g_regionView->showSea = i_v;
}

static void setMaxSelDeg(float i_v) {
  if (g_globeView)
    g_globeView->maxSelDeg = i_v;
}

static void setSimCellSize(float i_v) { g_simCellSize = i_v; }

static void setSimSpeed(float i_v) {
  g_simSpeed = i_v;
  if (simRunning() && !g_simAutoSpeed)
    g_sim->setTimeScale(g_simSpeed);
}

static void setSimAutoSpeed(bool i_v) {
  g_simAutoSpeed = i_v;
  if (simRunning() && !i_v)
    g_sim->setTimeScale(g_simSpeed);
}

static void startSimAction() {
  if (!g_dispComputing)
    startSimulation();
}

static void stopSimAction() { stopSimulation(); }

static emscripten::val getScenarios() {
  using emscripten::val;
  val l_arr = val::array();
  for (int l_i = 0; l_i < gv::k_numScenarios; l_i++) {
    const gv::Scenario& l_s = gv::k_scenarios[l_i];
    val l_o = val::object();
    l_o.set("name", std::string(l_s.name));
    l_o.set("mw", l_s.magnitude);
    l_o.set("epiLon", l_s.epiLon);
    l_o.set("epiLat", l_s.epiLat);
    l_arr.set(l_i, l_o);
  }
  return l_arr;
}

// Snapshot of everything the JS panel renders; polled by the UI.
static emscripten::val getState() {
  using emscripten::val;
  val o = val::object();
  o.set("state", g_state == AppState::REGION_SELECT ? std::string("globe")
                                                    : std::string("region"));
  o.set("error", g_regionError);
  o.set("placingStation", g_placingStation);

  val l_sel = val::object();
  const bool l_has = g_globeView && g_globeView->hasSelection();
  l_sel.set("has", l_has);
  if (l_has) {
    gv::BBox l_b = g_globeView->getSelection();
    l_sel.set("lonMin", (double)l_b.lonMin);
    l_sel.set("lonMax", (double)l_b.lonMax);
    l_sel.set("latMin", (double)l_b.latMin);
    l_sel.set("latMax", (double)l_b.latMax);
  }
  o.set("selection", l_sel);

  if (g_regionView) {
    val l_r = val::object();
    l_r.set("loaded", g_regionView->loaded());
    l_r.set("lonMin", g_regionView->lonMin);
    l_r.set("lonMax", g_regionView->lonMax);
    l_r.set("latMin", g_regionView->latMin);
    l_r.set("latMax", g_regionView->latMax);
    l_r.set("gridW", g_regionView->gridW);
    l_r.set("gridH", g_regionView->gridH);
    l_r.set("field",
            g_regionView->field == gv::RegionView::Field::Displacement ? 1 : 0);
    l_r.set("showSea", g_regionView->showSea);
    l_r.set("vertExag", g_regionView->vertExaggeration);
    l_r.set("waveExag", g_regionView->waveExaggeration);
    l_r.set("waterAnom", g_regionView->waterAnom());
    o.set("region", l_r);
  }

  val l_q = val::object();
  l_q.set("hasClick", g_hasClick);
  l_q.set("mw", g_mw);
  l_q.set("epiLon", g_epiLon);
  l_q.set("epiLat", g_epiLat);
  l_q.set("computing", g_dispComputing);
  l_q.set("hasDisplacement", g_regionView && g_regionView->hasDisplacement());
  // Mirror buildDisplacementModel(): inside Slab2 coverage the interface
  // scaling applies, outside it falls back to Wells & Coppersmith.
  const bool l_iface =
      g_slab2 && (g_hasClick ? g_slabPt.valid : g_restrictToSlab2);
  disp::WellsCoppersmith::FaultGeometry l_geo =
      l_iface ? disp::SubductionScaling::fromMagnitude(g_mw)
              : disp::WellsCoppersmith::fromMagnitude(g_mw);
  l_q.set("slip", l_geo.slip);
  l_q.set("length", l_geo.length);
  l_q.set("width", l_geo.width);
  l_q.set("ifaceScaling", l_iface);
  l_q.set("inCoverage", g_hasClick && g_slabPt.valid);
  if (g_hasClick && g_slabPt.valid) {
    l_q.set("slabDepth", g_slabPt.depth);
    l_q.set("slabStrike", g_slabPt.strike);
    l_q.set("slabDip", g_slabPt.dip);
    l_q.set("slabRegion",
            g_slabPt.region ? std::string(g_slabPt.region) : std::string());
  }
  o.set("quake", l_q);

  val l_sl = val::object();
  l_sl.set("available", (bool)g_slab2);
  l_sl.set("restrict", g_restrictToSlab2);
  l_sl.set("showOverlay", g_globeView && g_globeView->showSlab2Overlay);
  o.set("slab", l_sl);

  val l_s = val::object();
  const bool l_run = simRunning();
  l_s.set("running", l_run);
  l_s.set("cellSize", g_simCellSize);
  l_s.set("speed", g_simSpeed);
  l_s.set("autoSpeed", g_simAutoSpeed);
  if (l_run) {
    l_s.set("time", g_sim->simTime());
    l_s.set("steps", (double)g_sim->steps());
    l_s.set("maxSpeed", g_sim->maxTimeScale());
  }
  // Live preview of the grid the current settings would produce.
  if (g_regionView && g_regionView->loaded()) {
    double l_w = 0.0, l_h = 0.0;
    regionMetres(g_regionView->lonMin, g_regionView->lonMax,
                 g_regionView->latMin, g_regionView->latMax, l_w, l_h);
    tsunami_lab::t_idx l_nx = 0, l_ny = 0;
    double l_dxy = 0.0;
    simGridFor(l_w, l_h, g_simCellSize, l_nx, l_ny, l_dxy);
    l_s.set("previewNx", (double)l_nx);
    l_s.set("previewNy", (double)l_ny);
    l_s.set("effCellSize", l_dxy);
  }
  o.set("sim", l_s);
  return o;
}

EMSCRIPTEN_BINDINGS(tsunami_web) {
  using namespace emscripten;
  function("boot", &boot);
  function("renderFrame", &frame);
  function("resize", &resize);
  function("getState", &getState);
  function("getScenarios", &getScenarios);
  function("loadScenarioBytes", &loadScenarioBytes);
  function("loadSlab2Bytes", &loadSlab2Bytes);
  function("setRestrictToSlab2", &setRestrictToSlab2);
  function("setShowSlabOverlay", &setShowSlabOverlay);
  function("getHoverInfo", &getHoverInfo);
  function("loadSelection", &loadSelection);
  function("loadSelectionTiles", &loadSelectionTiles);
  function("backToGlobe", &backToGlobe);
  function("reloadRegion", &reloadRegion);
  function("setSelection", &setSelection);
  function("clearSelection", &clearSelection);
  function("setMaxSelDeg", &setMaxSelDeg);
  function("setMw", &setMw);
  function("commitMw", &commitMw);
  function("clearQuake", &clearQuake);
  function("setPlacingStation", &setPlacingStation);
  function("addStation", &addStation);
  function("renameStation", &renameStation);
  function("removeStation", &removeStation);
  function("clearStations", &clearStations);
  function("getStations", &getStations);
  function("setStationMarkers", &setStationMarkers);
  function("setField", &setField);
  function("setVertExaggeration", &setVertExaggeration);
  function("setWaveExaggeration", &setWaveExaggeration);
  function("setShowSea", &setShowSea);
  function("setSimCellSize", &setSimCellSize);
  function("setSimSpeed", &setSimSpeed);
  function("setSimAutoSpeed", &setSimAutoSpeed);
  function("startSim", &startSimAction);
  function("stopSim", &stopSimAction);
}
