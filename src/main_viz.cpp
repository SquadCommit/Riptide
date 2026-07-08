#include "displacement/OkadaDisplacement.h"
#include "displacement/OkadaFactory.h"
#include "displacement/SubductionScaling.h"
#include "displacement/WellsCoppersmith.h"
#include "io/Slab2Reader.h"
#include "util/CpuAffinity.h"
#include "util/OmpDefaults.h"
#include "visualization/Camera.h"
#include "visualization/Gebco.h"
#include "visualization/GlobeView.h"
#include "visualization/RegionView.h"
#include "visualization/Scenario.h"
#include "visualization/SimBuffer.h"
#include "visualization/SolverThread.h"
#include "visualization/Ui.h"
#include "visualization/Window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

// Application state

enum class AppState { REGION_SELECT, REGION_PREVIEW };

static AppState g_state = AppState::REGION_SELECT;

// Global input state (filled by GLFW callbacks)

static tsunami_lab::visualization::Camera* g_camera = nullptr;
static tsunami_lab::visualization::GlobeView* g_globeView = nullptr;
static tsunami_lab::visualization::RegionView* g_regionView = nullptr;

// Path to the local GEBCO grid (resolved/downloaded at startup).
static std::string g_gebcoPath;
// Set when a region read fails, so the UI can show feedback.
static std::string g_regionError;
// Max samples per axis when reading GEBCO for the bathymetry view.
// 0 = native resolution (no stride); positive = cap (faster for large areas).
static int g_bathMaxDim = 0;
// Longitude sample count for the whole-globe terrain mesh (bound to the
// resolution slider in the globe view).
static int g_globeMaxDim =
    tsunami_lab::visualization::GlobeView::DEFAULT_LON_SAMPLES;
// Last selection that was successfully loaded (used by the region-view reload).
static tsunami_lab::visualization::BBox g_loadedSel;

static bool g_mouseLeft = false;
static bool g_mouseMiddle = false;
static double g_lastX = 0, g_lastY = 0;
static int g_screenW = 1280, g_screenH = 720;

// Distinguishes a click (place displacement) from a drag (rotate) in the
// region preview.
static double g_pressX = 0, g_pressY = 0;
static float g_dragDist = 0.0f;

// Earthquake source: the user picks a click location and a moment magnitude,
// and the fault geometry is derived from Wells & Coppersmith scaling plus the
// local Slab2 subduction geometry.
static float g_mw = 8.0f;                 // moment magnitude
static double g_epiLon = 0, g_epiLat = 0; // epicentre (click location, deg)
static float g_epiWorldX = 0.0f;          // epicentre in the region world frame
static float g_epiWorldZ = 0.0f;

// Slab2 subduction-geometry reader (null when no grid is configured →
// fallback).
static std::unique_ptr<tsunami_lab::io::Slab2Reader> g_slab2;

// When true, an earthquake can only be triggered inside Slab2 coverage; when
// false, fallback parameters are used outside coverage.
static bool g_restrictToSlab2 = true;

// Fault orientation/depth used when no Slab2 grid is available.
static constexpr double k_fallbackDepth = 20000.0; // 20 km (m)
static constexpr double k_fallbackStrike = 0.0;    // deg
static constexpr double k_fallbackDip = 15.0;      // deg
static constexpr double k_rake = 90.0;             // pure thrust (fixed)

// Slab2 sample at the current click (or fallback); valid == false ⇒ no
// coverage.
static bool g_hasClick = false;
static tsunami_lab::io::Slab2Point g_slabPt = {0.0, 0.0, 0.0, false, nullptr};

// The displacement currently applied to the preview; null until a click's
// background build finishes (or after clearing).
static std::unique_ptr<tsunami_lab::displacement::OkadaDisplacement>
    g_displModel;

// Background displacement build: the Okada field over the full terrain grid
// takes seconds on large regions, so a click only launches a worker thread
// and pollDisplacementJob() uploads the result on the GL thread. While a job
// runs, clicks are ignored and grid-changing actions are disabled (the worker
// reads the RegionView grid).
struct DispResult {
  std::vector<float> disp;
  float peak;
};
static std::future<DispResult> g_dispFuture;
static bool g_dispComputing = false;
// Magnitude changed while a job was running → rebuild once it finishes.
static bool g_dispDirty = false;
// Model handed to the worker; becomes g_displModel when the job completes.
static std::unique_ptr<tsunami_lab::displacement::OkadaDisplacement>
    g_pendingModel;

// Builds the Okada displacement for the current magnitude and click location.
// With a Slab2 grid loaded, the factory supplies depth/strike/dip where the
// slab is present. Outside coverage the result depends on g_restrictToSlab2:
// restricted → nullptr (no fault); unrestricted → fallback orientation/depth.
static std::unique_ptr<tsunami_lab::displacement::OkadaDisplacement>
buildDisplacementModel() {
  namespace disp = tsunami_lab::displacement;
  if (g_slab2) {
    std::unique_ptr<disp::OkadaDisplacement> l_model =
        disp::OkadaFactory::fromMagnitudeAndLocation(g_mw, g_epiLon, g_epiLat,
                                                     *g_slab2, k_rake);
    if (l_model)
      return l_model; // inside Slab2 coverage
    if (g_restrictToSlab2)
      return nullptr; // outside coverage and restricted
    // else fall through to fallback parameters
  }

  disp::WellsCoppersmith::FaultGeometry l_geo =
      disp::WellsCoppersmith::fromMagnitude(g_mw);
  return std::unique_ptr<disp::OkadaDisplacement>(new disp::OkadaDisplacement(
      k_fallbackStrike, k_fallbackDip, k_rake, l_geo.slip, l_geo.length,
      l_geo.width, k_fallbackDepth));
}

// Live simulation (created on "Simulieren", destroyed on stop / leaving view).
static std::unique_ptr<tsunami_lab::visualization::SimBuffer> g_simBuf;
static std::unique_ptr<tsunami_lab::visualization::SolverThread> g_sim;
// Simulation resolution: edge length of one cell in metres. The cell count
// follows from the region size. Capped per axis (see simGridFor) so a fine
// resolution on a huge region cannot blow up the grid.
static float g_simCellSize = 1000.0f;
// Playback speed: simulated seconds advanced per real second.
static float g_simSpeed = 120.0f;
// When set, the playback speed follows the largest rate the CPU can sustain.
static bool g_simAutoSpeed = false;

// Intersect the ray through pixel (i_mx, i_my) with the y=0 sea-level plane and
// return the world (x, z) hit point.
static glm::vec2
regionUnproject(float i_mx,
                float i_my,
                int i_w,
                int i_h,
                const tsunami_lab::visualization::Camera& i_cam) {
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

// Selection rectangle proposed when the user clicks a subduction zone on the
// world map: centred on the click, sized for a typical tsunami run and clamped
// to the world bounds and the configured per-axis selection limit.
static tsunami_lab::visualization::BBox
suggestSlabSelection(double i_lon, double i_lat, float i_maxSelDeg) {
  const double l_lonSpan = std::min(12.0, (double)i_maxSelDeg);
  const double l_latSpan = std::min(8.0, (double)i_maxSelDeg);
  double l_lonMin = i_lon - 0.5 * l_lonSpan;
  double l_latMin = i_lat - 0.5 * l_latSpan;
  l_lonMin = std::min(std::max(l_lonMin, -180.0), 180.0 - l_lonSpan);
  l_latMin = std::min(std::max(l_latMin, -90.0), 90.0 - l_latSpan);
  tsunami_lab::visualization::BBox l_b;
  l_b.lonMin = (float)l_lonMin;
  l_b.lonMax = (float)(l_lonMin + l_lonSpan);
  l_b.latMin = (float)l_latMin;
  l_b.latMax = (float)(l_latMin + l_latSpan);
  return l_b;
}

// Launches the background displacement build for the current click and
// magnitude. No-op outside Slab2 coverage (restricted) or while a job runs.
static void startDisplacementJob() {
  if (!g_hasClick || !g_regionView || g_dispComputing)
    return;
  std::unique_ptr<tsunami_lab::displacement::OkadaDisplacement> l_model =
      buildDisplacementModel();
  if (!l_model)
    return; // outside Slab2 coverage
  g_pendingModel = std::move(l_model);
  g_dispComputing = true;
  g_dispDirty = false;

  const tsunami_lab::visualization::RegionView* l_view = g_regionView;
  const tsunami_lab::displacement::OkadaDisplacement* l_m =
      g_pendingModel.get();
  const float l_x = g_epiWorldX, l_z = g_epiWorldZ;
  g_dispFuture = std::async(std::launch::async, [l_view, l_m, l_x, l_z]() {
    DispResult l_r;
    l_view->computeDisplacementField(l_x, l_z, *l_m, l_r.disp, l_r.peak);
    return l_r;
  });
}

// Uploads a finished background build on the GL thread; called every frame.
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

// Record the clicked spot as the earthquake epicentre, sample the Slab2
// geometry there and immediately launch the displacement build.
static void onRegionClick(float i_mx, float i_my) {
  if (!g_regionView || !g_regionView->loaded() || !g_camera)
    return;
  if (g_dispComputing)
    return; // previous click's displacement is still building — ignore
  glm::vec2 l_world =
      regionUnproject(i_mx, i_my, g_screenW, g_screenH, *g_camera);
  g_epiWorldX = l_world.x;
  g_epiWorldZ = l_world.y;
  g_regionView->worldToLonLat(l_world.x, l_world.y, g_epiLon, g_epiLat);

  if (g_slab2)
    g_slabPt = g_slab2->query(g_epiLon, g_epiLat);
  else
    g_slabPt = {k_fallbackDepth, k_fallbackStrike, k_fallbackDip, true,
                nullptr};
  g_hasClick = true;

  // A new epicentre invalidates any previously applied displacement.
  g_displModel.reset();
  g_regionView->clearDisplacement();
  startDisplacementJob();
}

// Live simulation setup

// Bilinear sample of a row-major w×h grid at fractional (i_fx, i_fy), clamped.
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

// Metric width/height (metres) of a lon/lat box at its centre latitude.
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

// Grid (nx × ny) and effective cell size for a region of the given metric size
// at the requested resolution i_cellSize (m). If the resulting grid would
// exceed the per-axis cap, the cell size is coarsened so the grid stays square
// and bounded; o_dxy then reports the effective resolution actually used.
static void simGridFor(double i_widthM,
                       double i_heightM,
                       float i_cellSize,
                       tsunami_lab::t_idx& o_nx,
                       tsunami_lab::t_idx& o_ny,
                       double& o_dxy) {
  using tsunami_lab::t_idx;
  constexpr t_idx k_cap = 4000; // safety ceiling per axis
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

// Resamples the selected region onto a square-cell grid and builds the initial
// bathymetry + water column (still water to sea level, plus the placed quake
// uplift). Returns false if the GEBCO read fails.
static bool buildSimSetup(tsunami_lab::t_idx& o_nx,
                          tsunami_lab::t_idx& o_ny,
                          float& o_dxy,
                          std::vector<float>& o_bath,
                          std::vector<float>& o_height) {
  using namespace tsunami_lab;
  namespace gv = tsunami_lab::visualization;

  if (g_gebcoPath.empty() || !g_loadedSel.valid())
    return false;

  gv::gebco::Region l_src;
  if (!gv::gebco::readRegion(g_gebcoPath, g_loadedSel, l_src, 1200) ||
      l_src.w < 2 || l_src.h < 2)
    return false;

  const double l_latC = 0.5 * (l_src.latMin + l_src.latMax);
  const double l_mPerLat = 111132.0;
  const double l_mPerLon = 111320.0 * std::cos(l_latC * M_PI / 180.0);
  // Metric cell size of the GEBCO source raster, for the bathymetry-gradient
  // (Tanioka & Satake 1996) correction below.
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

  // Square cells at the requested resolution (cell size in metres); the grid
  // count follows from the region size, capped per axis for real-time stepping.
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
          l_uz = displacement::effectiveVerticalDisplacement(
              l_uz, l_uEast, l_uNorth, l_gradEast, l_gradNorth);
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
  namespace gv = tsunami_lab::visualization;

  stopSimulation();

  t_idx l_nx = 0, l_ny = 0;
  float l_dxy = 0.0f;
  std::vector<float> l_bath, l_height;
  if (!buildSimSetup(l_nx, l_ny, l_dxy, l_bath, l_height)) {
    g_regionError = "Simulation: GEBCO-Setup fehlgeschlagen.";
    return;
  }

  g_simBuf.reset(new gv::SimBuffer(l_nx, l_ny));
  g_sim.reset(new gv::SolverThread(*g_simBuf, l_nx, l_ny, l_dxy));

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
  g_sim->setTimeScale(g_simSpeed);
  g_sim->start();
}

static bool simRunning() { return g_sim && g_sim->running(); }

static void onMouseButton(GLFWwindow*, int i_btn, int i_act, int) {
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  if (i_btn == GLFW_MOUSE_BUTTON_LEFT) {
    bool pressed = (i_act == GLFW_PRESS);
    g_mouseLeft = pressed;

    if (g_state == AppState::REGION_SELECT && g_globeView) {
      if (pressed) {
        g_pressX = g_lastX;
        g_pressY = g_lastY;
        g_globeView->onMousePress((float)g_lastX, (float)g_lastY, g_screenW,
                                  g_screenH, *g_camera);
      } else {
        g_globeView->onMouseRelease();
        // Released without dragging on a subduction zone: propose a selection
        // rectangle around that spot as a quick start for a simulation region.
        const bool l_isClick =
            std::abs(g_lastX - g_pressX) + std::abs(g_lastY - g_pressY) < 5.0;
        if (l_isClick && g_slab2 && g_globeView->showSlab2Overlay && g_camera) {
          const glm::vec2 l_w = regionUnproject(
              (float)g_lastX, (float)g_lastY, g_screenW, g_screenH, *g_camera);
          // World X = lon, Z = -lat on the flat map.
          if (g_slab2->query(l_w.x, -l_w.y).valid)
            g_globeView->setSelection(
                suggestSlabSelection(l_w.x, -l_w.y, g_globeView->maxSelDeg));
        }
      }
    } else if (g_state == AppState::REGION_PREVIEW) {
      if (pressed) {
        g_pressX = g_lastX;
        g_pressY = g_lastY;
        g_dragDist = 0.0f;
      } else if (g_dragDist < 5.0f && !simRunning()) {
        // Released without dragging: treat as a click and pick an epicentre.
        // Disabled while a simulation is running (the field is live then).
        onRegionClick((float)g_lastX, (float)g_lastY);
      }
    }
  }
  if (i_btn == GLFW_MOUSE_BUTTON_MIDDLE)
    g_mouseMiddle = (i_act == GLFW_PRESS);
}

static void onCursorPos(GLFWwindow*, double i_x, double i_y) {
  float dx = (float)(i_x - g_lastX);
  float dy = (float)(i_y - g_lastY);
  g_lastX = i_x;
  g_lastY = i_y;

  if (!g_camera)
    return;

  if (g_state == AppState::REGION_SELECT) {
    // Left drag → selection rectangle
    if (g_mouseLeft && g_globeView)
      g_globeView->onMouseMove((float)i_x, (float)i_y, g_screenW, g_screenH,
                               *g_camera);
    // Middle drag → flat-map pan
    if (g_mouseMiddle)
      g_camera->onMapPan(dx, dy);
  } else {
    // Simulation view: normal arcball
    if (g_mouseLeft) {
      g_dragDist += std::abs(dx) + std::abs(dy);
      g_camera->onMouseDrag(dx, dy);
    }
    if (g_mouseMiddle)
      g_camera->onMiddleDrag(dx, dy);
  }
}

static void onScroll(GLFWwindow*, double, double i_dy) {
  if (ImGui::GetIO().WantCaptureMouse)
    return;
  if (g_camera)
    g_camera->onScroll((float)i_dy);
}

// Camera helpers

static void setCameraGlobeView(tsunami_lab::visualization::Camera& cam) {
  // With azimuth=0 and the lat-negation in the vertex shader (Z = -lat):
  //   right = (+1,0,0) → east to the right  ✓
  //   screen-up ≈ (0, 0, -1) world dir → north (negative Z) at top  ✓
  cam.setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
  cam.setAzimuth(0.0f);
  cam.setElevation(1.5f); // nearly top-down (avoids lookAt singularity at π/2)
  cam.setDistance(300.0f);
}

static void setCameraRegionView(tsunami_lab::visualization::Camera& cam) {
  // The region mesh is normalised to ~200 world units across and centred at
  // the origin; an angled 3/4 view shows the relief.
  cam.setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
  cam.setAzimuth(0.0f);
  cam.setElevation(0.7f);
  cam.setDistance(330.0f);
}

// Reads the GEBCO bathymetry for i_sel and switches to the region-preview
// state. Shared by the manual "Bathymetrie laden" button and scenario
// presets. Sets g_regionError and returns false on failure.
static bool
loadRegionAndEnterPreview(const tsunami_lab::visualization::BBox& i_sel) {
  g_regionError.clear();
  if (g_gebcoPath.empty()) {
    g_regionError = "Keine GEBCO-Daten verfügbar.";
    return false;
  }
  tsunami_lab::visualization::gebco::Region l_reg;
  if (!tsunami_lab::visualization::gebco::readRegion(g_gebcoPath, i_sel, l_reg,
                                                     g_bathMaxDim) ||
      !g_regionView || !g_regionView->load(l_reg)) {
    g_regionError = "Laden der Bathymetrie fehlgeschlagen.";
    return false;
  }
  g_loadedSel = i_sel;
  g_state = AppState::REGION_PREVIEW;
  if (g_slab2)
    g_regionView->buildSlab2Overlay(*g_slab2);
  if (g_camera)
    setCameraRegionView(*g_camera);
  return true;
}

// One-click historical scenario: loads its region, then places its fixed
// epicentre and launches the displacement build for its magnitude — the same
// steps a manual region-load + terrain click would trigger. Simulation start
// is still left to the user (via the "Simulieren" button), so the resulting
// fault/magnitude stays inspectable before committing.
static void triggerScenario(const tsunami_lab::visualization::Scenario& i_sc) {
  if (g_dispComputing || !g_regionView)
    return;
  if (g_globeView)
    g_globeView->setSelection(i_sc.region);
  if (!loadRegionAndEnterPreview(i_sc.region))
    return;

  g_mw = i_sc.magnitude;
  g_epiLon = i_sc.epiLon;
  g_epiLat = i_sc.epiLat;
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

// ImGui panels

// Shared width of the two left-hand panels. Note the built-in ImGui font only
// covers ASCII + Latin-1 — characters beyond that (arrows, dashes, ellipses)
// render as '?', so UI strings must stick to that range.
static const float k_panelW = 360.0f;

static void drawGlobeUi(tsunami_lab::visualization::GlobeView& globeView) {
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(k_panelW, 0), ImGuiCond_Always);
  ImGui::Begin("##globe_panel", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
  // Wrap long lines at the panel edge and reserve label space right of the
  // sliders so neither text nor widget labels spill out of the panel.
  ImGui::PushTextWrapPos(0.0f);
  ImGui::PushItemWidth(-150.0f);

  ImGui::TextColored(ImVec4(1, 0.85f, 0.1f, 1), "Gebiet auswählen");
  ImGui::Separator();

  ImGui::TextWrapped("Linksklick + Ziehen:  Gebiet zeichnen\n"
                     "Klick auf Zone:       Gebiets-Vorschlag\n"
                     "Mittelklick + Ziehen: Karte verschieben\n"
                     "WASD / Pfeiltasten:   Karte verschieben\n"
                     "Scrollrad:            Zoom");
  ImGui::Spacing();

  ImGui::Spacing();
  ImGui::SeparatorText("Historische Szenarien");
  for (int l_i = 0; l_i < tsunami_lab::visualization::k_numScenarios; l_i++) {
    ImGui::PushID(l_i);
    if (ImGui::Button(tsunami_lab::visualization::k_scenarios[l_i].name,
                      ImVec2(-1, 0)))
      triggerScenario(tsunami_lab::visualization::k_scenarios[l_i]);
    ImGui::PopID();
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Einstellungen");
  ImGui::SliderFloat("Max. Ausdehnung (°)", &globeView.maxSelDeg, 2.0f, 500.0f);

  using GV = tsunami_lab::visualization::GlobeView;
  ImGui::SliderInt("Detailgrad (Welt)", &g_globeMaxDim, GV::MIN_LON_SAMPLES,
                   GV::MAX_LON_SAMPLES);
  ImGui::TextDisabled("Welt-Gitter: bis ~%d Punkte breit", g_globeMaxDim);
  if (g_globeMaxDim != globeView.resolution()) {
    if (ImGui::Button("Auflösung anwenden", ImVec2(-1, 0)))
      globeView.setResolution(g_globeMaxDim);
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Subduktionszonen");
  ImGui::Checkbox("Zonen anzeigen (Slab2)", &globeView.showSlab2Overlay);
  if (globeView.showSlab2Overlay)
    ImGui::TextDisabled("Hover: Zonen-Info · Klick: Gebiets-Vorschlag");

  ImGui::Spacing();
  ImGui::SeparatorText("Auswahl");
  {
    // Persistent input buffer — synced from mouse selection when no field is
    // being edited, so mouse drags and manual typing don't fight each other.
    static float s_lonMin = 0, s_lonMax = 0, s_latMin = 0, s_latMax = 0;
    if (globeView.hasSelection() && !ImGui::IsAnyItemActive()) {
      tsunami_lab::visualization::BBox l_cur = globeView.getSelection();
      s_lonMin = l_cur.lonMin;
      s_lonMax = l_cur.lonMax;
      s_latMin = l_cur.latMin;
      s_latMax = l_cur.latMax;
    }

    bool l_changed = false;
    const float l_half = ImGui::GetContentRegionAvail().x * 0.5f - 4;
    ImGui::Text("Lon");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(l_half);
    l_changed |= ImGui::InputFloat("##lonMin", &s_lonMin, 0, 0, "%.2f°");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    l_changed |= ImGui::InputFloat("##lonMax", &s_lonMax, 0, 0, "%.2f°");

    ImGui::Text("Lat");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(l_half);
    l_changed |= ImGui::InputFloat("##latMin", &s_latMin, 0, 0, "%.2f°");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    l_changed |= ImGui::InputFloat("##latMax", &s_latMax, 0, 0, "%.2f°");

    if (l_changed) {
      s_lonMin = std::max(s_lonMin, -180.0f);
      s_lonMax = std::min(s_lonMax, 180.0f);
      s_latMin = std::max(s_latMin, -90.0f);
      s_latMax = std::min(s_latMax, 90.0f);
      tsunami_lab::visualization::BBox l_nb;
      l_nb.lonMin = s_lonMin;
      l_nb.lonMax = s_lonMax;
      l_nb.latMin = s_latMin;
      l_nb.latMax = s_latMax;
      if (l_nb.valid())
        globeView.setSelection(l_nb);
    }

    if (globeView.hasSelection()) {
      tsunami_lab::visualization::BBox sel = globeView.getSelection();
      ImGui::TextDisabled("Lon %.1f°  Lat %.1f°", sel.lonSpan(), sel.latSpan());

      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.65f, 0.2f, 1));
      if (ImGui::Button("Bathymetrie laden  >>", ImVec2(-1, 0)))
        loadRegionAndEnterPreview(sel);
      ImGui::PopStyleColor();

      if (!g_regionError.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s",
                           g_regionError.c_str());

      ImGui::Spacing();
      if (ImGui::Button("Auswahl löschen", ImVec2(-1, 0)))
        globeView.clearSelection();
    } else {
      ImGui::TextDisabled("Werte eingeben oder Bereich auf Globus ziehen.");
    }
  }

  ImGui::PopItemWidth();
  ImGui::PopTextWrapPos();
  ImGui::End();
}

// Hover info on the world map: while the cursor is over a subduction zone,
// show the zone's name and local Slab2 geometry next to the mouse.
static void
drawGlobeSlabTooltip(const tsunami_lab::visualization::GlobeView& i_globe) {
  if (g_state != AppState::REGION_SELECT || !g_slab2 ||
      !i_globe.showSlab2Overlay || !g_camera)
    return;
  if (ImGui::GetIO().WantCaptureMouse || g_mouseLeft)
    return; // over a panel, or dragging a selection

  const glm::vec2 l_w = regionUnproject((float)g_lastX, (float)g_lastY,
                                        g_screenW, g_screenH, *g_camera);
  const double l_lon = l_w.x, l_lat = -l_w.y; // world X = lon, Z = -lat
  if (l_lon < -180.0 || l_lon > 180.0 || l_lat < -90.0 || l_lat > 90.0)
    return;

  const tsunami_lab::io::Slab2Point l_p = g_slab2->query(l_lon, l_lat);
  if (!l_p.valid)
    return;

  ImGui::BeginTooltip();
  ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.25f, 1), "%s",
                     l_p.region ? l_p.region : "Subduktionszone");
  ImGui::Text("Tiefe:     %.0f km", l_p.depth / 1000.0);
  ImGui::Text("Streichen: %.0f°", l_p.strike);
  ImGui::Text("Einfallen: %.0f°", l_p.dip);
  ImGui::TextDisabled("Klick: Gebiets-Vorschlag");
  ImGui::EndTooltip();
}

static void drawRegionUi(tsunami_lab::visualization::RegionView& regionView) {
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(k_panelW, 0), ImGuiCond_Always);
  ImGui::Begin("##region_panel", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
  // Wrap long lines at the panel edge and reserve label space right of the
  // sliders so neither text nor widget labels spill out of the panel.
  ImGui::PushTextWrapPos(0.0f);
  ImGui::PushItemWidth(-150.0f);

  ImGui::TextColored(ImVec4(0.2f, 0.8f, 1, 1), "Bathymetrie-Vorschau");
  ImGui::Separator();

  ImGui::Text("Lon: %.2f° - %.2f°", regionView.lonMin, regionView.lonMax);
  ImGui::Text("Lat: %.2f° - %.2f°", regionView.latMin, regionView.latMax);
  ImGui::Text("Gitter: %d × %d", regionView.gridW, regionView.gridH);
  ImGui::Spacing();

  ImGui::TextWrapped("Linksklick + Ziehen:  Drehen\n"
                     "Mittelklick + Ziehen: Verschieben\n"
                     "WASD / Pfeiltasten:   Verschieben\n"
                     "Scrollrad:            Zoom");
  ImGui::Spacing();

  ImGui::SeparatorText("Auflösung");
  {
    bool l_native = (g_bathMaxDim == 0);
    if (ImGui::Checkbox("Nativ (volle GEBCO-Auflösung)", &l_native))
      g_bathMaxDim = l_native ? 0 : 4000;
    if (!l_native) {
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderInt("Max. Samples", &g_bathMaxDim, 500, 16000);
    }
    // The displacement worker reads the current grid — no reload while it
    // runs.
    ImGui::BeginDisabled(g_dispComputing);
    if (ImGui::Button("Neu laden", ImVec2(-1, 0)) && g_loadedSel.valid() &&
        !g_gebcoPath.empty()) {
      tsunami_lab::visualization::gebco::Region l_reg;
      if (tsunami_lab::visualization::gebco::readRegion(
              g_gebcoPath, g_loadedSel, l_reg, g_bathMaxDim) &&
          regionView.load(l_reg)) {
        regionView.clearDisplacement();
        g_displModel.reset();
        if (g_slab2)
          regionView.buildSlab2Overlay(*g_slab2);
      } else
        g_regionError = "Neu laden fehlgeschlagen.";
    }
    ImGui::EndDisabled();
    if (!g_regionError.empty())
      ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", g_regionError.c_str());
  }
  ImGui::Spacing();

  ImGui::SeparatorText("Darstellung");
  using Field = tsunami_lab::visualization::RegionView::Field;
  int l_field = (regionView.field == Field::Displacement) ? 1 : 0;
  ImGui::RadioButton("Bathymetrie", &l_field, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Displacement", &l_field, 1);
  regionView.field = (l_field == 1) ? Field::Displacement : Field::Bathymetry;
  ImGui::SliderFloat("Überhöhung", &regionView.vertExaggeration, 1.0f, 100.0f,
                     "%.0f×");
  ImGui::SliderFloat("Wellen-Überhöhung", &regionView.waveExaggeration, 1.0f,
                     5000.0f, "%.0f×", ImGuiSliderFlags_Logarithmic);
  ImGui::Checkbox("Meeresspiegel anzeigen", &regionView.showSea);
  ImGui::Checkbox("Show subduction zones", &regionView.showSlab2Overlay);

  ImGui::Spacing();
  ImGui::SeparatorText("Erdbebenquelle");
  ImGui::TextWrapped("Klick auf das Gelände setzt das Epizentrum und "
                     "erzeugt das Displacement im Hintergrund.");

  ImGui::Checkbox("Restrict to subduction zones", &g_restrictToSlab2);
  if (!g_restrictToSlab2) {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                       "! Fallback parameters outside Slab2 coverage");
    ImGui::Text("  depth=20km  strike=0deg  dip=15deg");
  }

  ImGui::SliderFloat("Magnitude (Mw)", &g_mw, 6.0f, 9.5f, "%.1f");
  // Without a generate button, releasing the slider rebuilds the displacement
  // for the current epicentre; mid-build changes are queued via g_dispDirty.
  if (ImGui::IsItemDeactivatedAfterEdit() && g_hasClick && !simRunning()) {
    if (g_dispComputing)
      g_dispDirty = true;
    else
      startDisplacementJob();
  }

  // Fault geometry derived from the magnitude (updates live as Mw changes).
  // Mirror buildDisplacementModel(): inside Slab2 coverage the factory uses
  // the subduction-interface scaling, outside it falls back to Wells &
  // Coppersmith. Before any click, assume the interface case whenever only
  // in-coverage clicks can trigger a quake.
  namespace disp = tsunami_lab::displacement;
  const bool l_ifaceScaling =
      g_slab2 && (g_hasClick ? g_slabPt.valid : g_restrictToSlab2);
  disp::WellsCoppersmith::FaultGeometry l_geo =
      l_ifaceScaling ? disp::SubductionScaling::fromMagnitude(g_mw)
                     : disp::WellsCoppersmith::fromMagnitude(g_mw);
  ImGui::Text("Versatz: %.2f m", l_geo.slip);
  ImGui::Text("Länge:   %.0f km", l_geo.length / 1000.0);
  ImGui::Text("Breite:  %.0f km", l_geo.width / 1000.0);
  ImGui::TextDisabled(l_ifaceScaling
                          ? "Skalierung: Strasser et al. 2010 (Interface)"
                          : "Skalierung: Wells & Coppersmith 1994");

  if (g_slab2 == nullptr)
    ImGui::TextColored(
        ImVec4(1, 0.6f, 0.2f, 1),
        "Slab2 nicht geladen - Fallback\n(Tiefe=20km, Streichen=0°, Dip=15°)");

  // Depth/strike/dip come from Slab2 at the clicked location.
  const bool l_inCoverage = g_hasClick && g_slabPt.valid;
  if (l_inCoverage) {
    if (g_slabPt.region)
      ImGui::Text("Zone:      %s", g_slabPt.region);
    ImGui::Text("Tiefe:     %.0f km  (Slab2)", g_slabPt.depth / 1000.0);
    ImGui::Text("Streichen: %.0f°  (Slab2)", g_slabPt.strike);
    ImGui::Text("Einfallen: %.0f°  (Slab2)", g_slabPt.dip);
  } else if (g_hasClick) {
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                       "Außerhalb der Slab2-Abdeckung");
  } else {
    ImGui::Text("Tiefe:     -");
    ImGui::Text("Streichen: -");
    ImGui::Text("Einfallen: -");
  }
  ImGui::Text("Rake:      90° (fest)");

  if (g_hasClick)
    ImGui::Text("Epizentrum: %.2f°, %.2f°", g_epiLon, g_epiLat);

  if (g_dispComputing) {
    ImGui::TextColored(ImVec4(1, 0.85f, 0.1f, 1),
                       "Displacement wird berechnet ...");
  } else if (regionView.hasDisplacement()) {
    if (ImGui::Button("Displacement entfernen", ImVec2(-1, 0))) {
      regionView.clearDisplacement();
      g_displModel.reset();
    }
  } else if (!g_hasClick) {
    ImGui::TextDisabled("(noch kein Epizentrum)");
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Simulation");
  ImGui::SliderFloat("Auflösung (m/Zelle)", &g_simCellSize, 50.0f, 20000.0f,
                     "%.0f m", ImGuiSliderFlags_Logarithmic);
  {
    // Live preview of the resulting grid from the current selection bounds.
    double l_w = 0.0, l_h = 0.0;
    regionMetres(regionView.lonMin, regionView.lonMax, regionView.latMin,
                 regionView.latMax, l_w, l_h);
    tsunami_lab::t_idx l_pnx = 0, l_pny = 0;
    double l_pdxy = 0.0;
    simGridFor(l_w, l_h, g_simCellSize, l_pnx, l_pny, l_pdxy);
    if (l_pdxy > g_simCellSize * 1.01)
      ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1),
                         "-> %zu × %zu Zellen (begrenzt: eff. %.0f m)",
                         (size_t)l_pnx, (size_t)l_pny, l_pdxy);
    else
      ImGui::TextDisabled("-> %zu × %zu Zellen", (size_t)l_pnx, (size_t)l_pny);
  }
  ImGui::Checkbox("Zeitraffer automatisch (CPU-Limit)", &g_simAutoSpeed);
  if (g_simAutoSpeed) {
    // Couple playback to the largest rate the hardware sustains, so the wave
    // never silently falls behind the requested speed.
    const double l_max = simRunning() ? g_sim->maxTimeScale() : 0.0;
    if (simRunning())
      g_sim->setTimeScale(l_max); // 0 until measured ⇒ uncapped warm-up
    ImGui::BeginDisabled();
    ImGui::SliderFloat("Zeitraffer (Sim-s/s)", &g_simSpeed, 1.0f, 2000.0f,
                       "%.0f×", ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();
    if (l_max > 0.0)
      ImGui::Text("läuft bei ~%.0f× (CPU-Limit)", l_max);
    else
      ImGui::TextDisabled("(CPU-Limit wird gemessen ...)");
  } else {
    ImGui::SliderFloat("Zeitraffer (Sim-s/s)", &g_simSpeed, 1.0f, 2000.0f,
                       "%.0f×", ImGuiSliderFlags_Logarithmic);
    if (simRunning()) {
      g_sim->setTimeScale(g_simSpeed);
      const double l_max = g_sim->maxTimeScale();
      if (l_max > 0.0 && g_simSpeed > l_max * 1.05)
        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1),
                           "CPU schafft nur ~%.0f× - Rest wird gekappt.",
                           l_max);
    }
  }
  if (!simRunning()) {
    // Wait for a running displacement build: its result seeds the initial
    // condition, so starting earlier would simulate without the quake.
    ImGui::BeginDisabled(g_dispComputing);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.65f, 0.2f, 1));
    if (ImGui::Button("Simulieren  >>", ImVec2(-1, 0)))
      startSimulation();
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
  } else {
    {
      const double l_simT = g_sim->simTime();
      const int l_h = (int)(l_simT / 3600.0);
      const int l_m = (int)(l_simT / 60.0) % 60;
      const int l_s = (int)l_simT % 60;
      ImGui::TextColored(ImVec4(0.2f, 0.8f, 1, 1), "läuft - %zu Schritte",
                         (size_t)g_sim->steps());
      if (l_h > 0)
        ImGui::TextColored(ImVec4(1, 0.85f, 0.1f, 1),
                           "Seit Erdbeben: %d h %02d min %02d s", l_h, l_m,
                           l_s);
      else if (l_m > 0)
        ImGui::TextColored(ImVec4(1, 0.85f, 0.1f, 1),
                           "Seit Erdbeben: %d min %02d s", l_m, l_s);
      else
        ImGui::TextColored(ImVec4(1, 0.85f, 0.1f, 1), "Seit Erdbeben: %.1f s",
                           l_simT);
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.15f, 1));
    if (ImGui::Button("Stop", ImVec2(-1, 0)))
      stopSimulation();
    ImGui::PopStyleColor();
  }

  ImGui::Spacing();
  // Leaving could load a new grid while the displacement worker still reads
  // the current one — wait for it.
  ImGui::BeginDisabled(g_dispComputing);
  if (ImGui::Button("<< Zurück zur Gebietsauswahl", ImVec2(-1, 0))) {
    stopSimulation();
    g_state = AppState::REGION_SELECT;
    if (g_camera)
      setCameraGlobeView(*g_camera);
  }
  ImGui::EndDisabled();

  ImGui::End();
}

// One colour stop along a horizontal legend bar (t in [0,1] = left→right).
struct LegendStop {
  float t;
  ImU32 c;
};

// Shared geometry so the two legends stack cleanly in the bottom-right corner.
static const float k_legW = 232.0f;
static const float k_legH = 80.0f;
static const float k_legPad = 12.0f;
static const float k_legGap = 8.0f;

// Horizontal colour-scale legend: title, gradient bar, end labels. Both the
// wave and terrain legends use this identical layout (only the colours and
// labels differ).
static void drawHLegend(const char* i_id,
                        ImVec2 i_pos,
                        const char* i_title,
                        const LegendStop* i_stops,
                        int i_n,
                        const char* i_left,
                        const char* i_right) {
  const float l_barW = 214.0f, l_barH = 16.0f;
  ImGui::SetNextWindowPos(i_pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(k_legW, k_legH), ImGuiCond_Always);
  ImGui::Begin(i_id, nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

  ImGui::TextUnformatted(i_title);

  ImDrawList* l_dl = ImGui::GetWindowDrawList();
  const ImVec2 l_p = ImGui::GetCursorScreenPos();
  for (int l_i = 0; l_i < i_n - 1; l_i++) {
    const float l_x0 = l_p.x + i_stops[l_i].t * l_barW;
    const float l_x1 = l_p.x + i_stops[l_i + 1].t * l_barW;
    if (l_x1 <= l_x0)
      continue; // zero-width segment (e.g. the coastline at sea level)
    l_dl->AddRectFilledMultiColor(
        ImVec2(l_x0, l_p.y), ImVec2(l_x1, l_p.y + l_barH), i_stops[l_i].c,
        i_stops[l_i + 1].c, i_stops[l_i + 1].c, i_stops[l_i].c);
  }
  ImGui::Dummy(ImVec2(l_barW, l_barH));

  const float l_tw = ImGui::CalcTextSize(i_right).x;
  ImGui::TextUnformatted(i_left);
  ImGui::SameLine(l_barW - l_tw);
  ImGui::TextUnformatted(i_right);

  ImGui::End();
}

// Terrain colour scale, anchored to the bottom-right corner. Stops mirror
// colormap() in region_terrain.frag / globe_terrain.frag, positioned by their
// elevation across the fixed [-6000, +4000] m domain (sea level at t = 0.6).
static void
drawBathyLegend(const tsunami_lab::visualization::RegionView& regionView) {
  using Field = tsunami_lab::visualization::RegionView::Field;
  if (!regionView.loaded() || regionView.field != Field::Bathymetry)
    return;

  static const LegendStop l_stops[] = {
      {0.00f, IM_COL32(8, 33, 97, 255)},      // -6000 deep ocean
      {0.30f, IM_COL32(23, 71, 148, 255)},    // -3000
      {0.50f, IM_COL32(46, 115, 194, 255)},   // -1000
      {0.58f, IM_COL32(84, 158, 212, 255)},   // -200 shelf
      {0.60f, IM_COL32(130, 189, 219, 255)},  // 0 shallow (coast)
      {0.60f, IM_COL32(69, 140, 69, 255)},    // 0 lowland green (coast)
      {0.61f, IM_COL32(115, 168, 82, 255)},   // 100
      {0.63f, IM_COL32(184, 186, 99, 255)},   // 300
      {0.66f, IM_COL32(199, 168, 115, 255)},  // 600
      {0.72f, IM_COL32(158, 122, 92, 255)},   // 1200
      {0.85f, IM_COL32(122, 102, 92, 255)},   // 2500
      {1.00f, IM_COL32(242, 242, 245, 255)}}; // 4000 snow
  const int l_n = (int)(sizeof(l_stops) / sizeof(l_stops[0]));

  const ImGuiIO& l_io = ImGui::GetIO();
  const ImVec2 l_pos(l_io.DisplaySize.x - k_legW - k_legPad,
                     l_io.DisplaySize.y - k_legH - k_legPad);
  drawHLegend("##bathy_legend", l_pos, "Höhe / Tiefe (m)", l_stops, l_n,
              "-6000 m", "+4000 m");
}

// Wave colour scale, stacked just above the terrain legend during simulation.
// The gradient mirrors jet() in region_water.frag.
static void
drawWaveLegend(const tsunami_lab::visualization::RegionView& regionView) {
  using Field = tsunami_lab::visualization::RegionView::Field;
  if (!regionView.simulating() || regionView.field != Field::Bathymetry)
    return;

  // Diverging colormap: negative (trough) left half, positive (crest) right
  // half. Mirrors wavemap() in region_water.frag; t in [0,1] maps to [-anom,
  // +anom].
  static const LegendStop l_stops[] = {
      {0.000f, IM_COL32(58, 5, 55, 255)},    // dark purple  (trough peak)
      {0.125f, IM_COL32(140, 13, 140, 255)}, // deep violet
      {0.250f, IM_COL32(107, 31, 199, 255)}, // violet
      {0.375f, IM_COL32(56, 71, 209, 255)},  // indigo
      {0.500f, IM_COL32(33, 87, 204, 255)},  // calm blue     (zero / sea level)
      {0.600f, IM_COL32(26, 158, 224, 255)}, // cyan
      {0.700f, IM_COL32(51, 204, 107, 255)}, // green
      {0.800f, IM_COL32(242, 224, 51, 255)}, // yellow
      {0.900f, IM_COL32(247, 133, 31, 255)}, // orange
      {1.000f, IM_COL32(209, 26, 23, 255)},  // red           (crest peak)
  };

  char l_neg[32], l_pos_buf[32];
  std::snprintf(l_neg, sizeof(l_neg), "-%.2f m", regionView.waterAnom());
  std::snprintf(l_pos_buf, sizeof(l_pos_buf), "+%.2f m",
                regionView.waterAnom());

  const ImGuiIO& l_io = ImGui::GetIO();
  const ImVec2 l_pos(l_io.DisplaySize.x - k_legW - k_legPad,
                     l_io.DisplaySize.y - 2.0f * k_legH - k_legGap - k_legPad);
  drawHLegend("##wave_legend", l_pos, "Wellenhöhe (Anomalie)", l_stops, 10,
              l_neg, l_pos_buf);
}

// Subduction-depth colour scale for the world map, anchored to the
// bottom-right corner like the region-view legends. Stops mirror
// slabDepthColor() in GlobeView.cpp (sqrt-warped depth scale, so the shallow
// trench side gets most of the gradient).
static void
drawSlabLegend(const tsunami_lab::visualization::GlobeView& i_globe) {
  if (!g_slab2 || !i_globe.showSlab2Overlay)
    return;

  static const LegendStop l_stops[] = {
      {0.00f, IM_COL32(255, 195, 60, 255)}, // trench (shallow)
      {0.35f, IM_COL32(255, 95, 40, 255)},
      {0.70f, IM_COL32(205, 45, 115, 255)},
      {1.00f, IM_COL32(115, 35, 165, 255)}}; // deepest slab
  const int l_n = (int)(sizeof(l_stops) / sizeof(l_stops[0]));

  const ImGuiIO& l_io = ImGui::GetIO();
  const ImVec2 l_pos(l_io.DisplaySize.x - k_legW - k_legPad,
                     l_io.DisplaySize.y - k_legH - k_legPad);
  drawHLegend("##slab_legend", l_pos, "Slab-Tiefe (Subduktion)", l_stops, l_n,
              "0 km (Graben)", "660 km");
}

// Hover cursor feedback: a coloured ring at the mouse while it is over the
// bathymetry. Green = inside Slab2 coverage; red = outside and restricted (no
// fault possible); yellow = outside but fallback parameters would be used.
static void
drawCursorFeedback(const tsunami_lab::visualization::RegionView& i_region) {
  if (g_state != AppState::REGION_PREVIEW || !i_region.loaded() || !g_camera)
    return;
  if (ImGui::GetIO().WantCaptureMouse || simRunning())
    return; // over a panel, or the field is live (clicks disabled)

  glm::vec2 l_world = regionUnproject((float)g_lastX, (float)g_lastY, g_screenW,
                                      g_screenH, *g_camera);
  double l_lon = 0.0, l_lat = 0.0;
  i_region.worldToLonLat(l_world.x, l_world.y, l_lon, l_lat);
  if (l_lon < i_region.lonMin || l_lon > i_region.lonMax ||
      l_lat < i_region.latMin || l_lat > i_region.latMax)
    return; // cursor is not over the region footprint

  const bool l_valid = g_slab2 && g_slab2->query(l_lon, l_lat).valid;
  ImU32 l_col;
  if (g_dispComputing)
    l_col = IM_COL32(160, 160, 160, 255); // grey: build running, clicks ignored
  else if (l_valid)
    l_col = IM_COL32(40, 220, 80, 255); // green: covered
  else if (g_restrictToSlab2)
    l_col = IM_COL32(230, 60, 50, 255); // red: no fault here
  else
    l_col = IM_COL32(240, 220, 40, 255); // yellow: fallback would apply

  const ImVec2 l_p((float)g_lastX, (float)g_lastY);
  ImDrawList* l_dl = ImGui::GetForegroundDrawList();
  l_dl->AddCircle(l_p, 10.0f, l_col, 0, 2.5f);
  l_dl->AddCircleFilled(l_p, 3.0f, l_col);
}

// Main

// Pins the GUI thread (this thread) to a single fixed core, keeping it off
// the cores OpenMP is told to use for the solver (see TSUNAMI_OMP_PLACES in
// sphinx/source/chapters/09_parallelization.rst). Opt-in via TSUNAMI_VIZ_CORE
// (logical CPU id) so builds/machines that don't care about this are
// unaffected by default.
static void pinGuiThreadFromEnv() {
  const char* l_env = std::getenv("TSUNAMI_VIZ_CORE");
  if (!l_env)
    return;
  int l_core = std::atoi(l_env);
  if (tsunami_lab::util::pinThreadToCore(l_core))
    std::printf("GUI-Thread an Kern %d gepinnt (TSUNAMI_VIZ_CORE).\n", l_core);
  else
    std::printf("Warnung: Konnte GUI-Thread nicht an Kern %d pinnen.\n",
                l_core);
}

int main() {
  tsunami_lab::util::applySaneOmpScheduleDefault();
  pinGuiThreadFromEnv();

  // Resolve (and, on first run, download) the GEBCO grid before opening the
  // window — the one-time download prints progress to the terminal.
  g_gebcoPath = tsunami_lab::visualization::gebco::ensureAvailable();

  // Slab2 subduction-geometry grids (global): enable click-driven Okada faults
  // with realistic depth/strike/dip anywhere a slab exists. Downloaded from
  // USGS ScienceBase on first run (like GEBCO). If unavailable, the source
  // panel falls back to fixed values.
  {
    if (tsunami_lab::io::Slab2Reader::ensureAvailable()) {
      g_slab2.reset(new tsunami_lab::io::Slab2Reader());
      std::printf("Slab2: globale Grids geladen.\n");
    } else {
      std::printf("Slab2: nicht verfügbar — Fallback aktiv.\n");
    }
  }

  tsunami_lab::visualization::Window l_window(1280, 720,
                                              "Tsunami Lab — Gebietsauswahl");
  tsunami_lab::visualization::Camera l_camera;
  tsunami_lab::visualization::GlobeView l_globe;
  tsunami_lab::visualization::RegionView l_region;
  tsunami_lab::visualization::Ui l_ui;

  g_camera = &l_camera;
  g_globeView = &l_globe;
  g_regionView = &l_region;

  glfwSetMouseButtonCallback(l_window.handle(), onMouseButton);
  glfwSetCursorPosCallback(l_window.handle(), onCursorPos);
  glfwSetScrollCallback(l_window.handle(), onScroll);

  l_ui.init(l_window.handle());

  std::printf("OpenGL %s\n", glGetString(GL_VERSION));
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE); // pairs with GLFW_SAMPLES in Window.cpp

  // Initialise globe overview (subsampled GEBCO) + region preview resources.
  std::printf("Lade GEBCO-Daten …\n");
  l_globe.init(g_gebcoPath.empty() ? nullptr : g_gebcoPath.c_str());
  if (g_slab2) {
    std::printf("Baue Subduktionszonen-Overlay …\n");
    l_globe.buildSlab2Overlay(*g_slab2);
  }
  l_region.init();
  std::printf("Fertig.\n");

  // Set camera for flat-map globe view
  setCameraGlobeView(l_camera);

  double g_lastFrameTime = glfwGetTime();

  while (!l_window.shouldClose()) {
    l_window.pollEvents();

    double l_now = glfwGetTime();
    float l_dt = (float)(l_now - g_lastFrameTime);
    g_lastFrameTime = l_now;

    if (!ImGui::GetIO().WantCaptureKeyboard && g_camera) {
      float l_speed = 200.0f * l_dt;
      float l_kx = 0.0f, l_ky = 0.0f;
      GLFWwindow* l_win = l_window.handle();
      if (glfwGetKey(l_win, GLFW_KEY_A) == GLFW_PRESS ||
          glfwGetKey(l_win, GLFW_KEY_LEFT) == GLFW_PRESS)
        l_kx = -l_speed;
      if (glfwGetKey(l_win, GLFW_KEY_D) == GLFW_PRESS ||
          glfwGetKey(l_win, GLFW_KEY_RIGHT) == GLFW_PRESS)
        l_kx = l_speed;
      if (glfwGetKey(l_win, GLFW_KEY_W) == GLFW_PRESS ||
          glfwGetKey(l_win, GLFW_KEY_UP) == GLFW_PRESS)
        l_ky = -l_speed;
      if (glfwGetKey(l_win, GLFW_KEY_S) == GLFW_PRESS ||
          glfwGetKey(l_win, GLFW_KEY_DOWN) == GLFW_PRESS)
        l_ky = l_speed;

      if (l_kx != 0.0f || l_ky != 0.0f) {
        if (g_state == AppState::REGION_SELECT ||
            g_state == AppState::REGION_PREVIEW)
          g_camera->onMapPan(-l_kx, -l_ky);
      }
    }

    // Framebuffer size (pixels) drives the viewport; window size (logical
    // points) drives mouse→world unprojection so it matches the cursor coords.
    int l_fbW, l_fbH;
    l_window.getSize(l_fbW, l_fbH);
    int l_winW, l_winH;
    l_window.getWindowSize(l_winW, l_winH);
    g_screenW = l_winW;
    g_screenH = l_winH;
    glViewport(0, 0, l_fbW, l_fbH);
    glClearColor(0.06f, 0.09f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (l_winH > 0) ? (float)l_winW / (float)l_winH : 1.0f;
    glm::mat4 vp = l_camera.projection(aspect) * l_camera.view();

    pollDisplacementJob();

    if (simRunning() && g_simBuf->swap())
      l_region.updateWater(g_simBuf->front());

    if (g_state == AppState::REGION_SELECT) {
      l_globe.lodCamDistance = l_camera.getDistance();
      l_globe.lodViewportPx = l_fbH;
      l_globe.draw(vp);
    } else {
      l_region.lodViewportPx = l_fbH;
      l_region.draw(vp, l_camera.position());
    }

    l_ui.beginFrame();
    if (g_state == AppState::REGION_SELECT) {
      drawGlobeUi(l_globe);
      drawGlobeSlabTooltip(l_globe);
      drawSlabLegend(l_globe);
    } else {
      drawRegionUi(l_region);
      drawBathyLegend(l_region);
      drawWaveLegend(l_region);
      drawCursorFeedback(l_region);
    }
    l_ui.endFrame();

    l_window.swapBuffers();
  }

  stopSimulation();
  l_ui.shutdown();
  return 0;
}
