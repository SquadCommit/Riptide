#include "RegionView.h"

#include "Lod.h"
#include "io/Slab2Reader.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace tsunami_lab {
namespace visualization {

// Inline shaders for the subduction-zone overlay: a flat sea-level quad
// textured with the Slab2 coverage, whose texture alpha drives transparency.
static const char* k_overlayVert = R"(#version 330 core
layout(location = 0) in vec2 aXZ;  // (x, z) world position at sea level
layout(location = 1) in vec2 aUV;
uniform mat4 uVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uVP * vec4(aXZ.x, 0.0, aXZ.y, 1.0);
}
)";

static const char* k_overlayFrag = R"(#version 330 core
in vec2 vUV;
uniform sampler2D u_overlay;
out vec4 FragColor;
void main() {
    FragColor = texture(u_overlay, vUV);
}
)";

void RegionView::init() {
  m_terrShader.buildFromFiles(SHADER_DIR "/region_terrain.vert",
                              SHADER_DIR "/region_terrain.frag");
  m_seaShader.buildFromFiles(SHADER_DIR "/region_sea.vert",
                             SHADER_DIR "/region_sea.frag");
  m_waterShader.buildFromFiles(SHADER_DIR "/region_water.vert",
                               SHADER_DIR "/region_water.frag");
  m_overlayShader.build(k_overlayVert, k_overlayFrag);

  const float k_s = 100.0f;
  const float l_quad[8] = {-k_s, -k_s, k_s, -k_s, -k_s, k_s, k_s, k_s};
  glGenVertexArrays(1, &m_seaVao);
  glGenBuffers(1, &m_seaVbo);
  glBindVertexArray(m_seaVao);
  glBindBuffer(GL_ARRAY_BUFFER, m_seaVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(l_quad), l_quad, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

bool RegionView::load(const gebco::Region& i_region) {
  const int l_w = i_region.w;
  const int l_h = i_region.h;
  if (l_w < 2 || l_h < 2)
    return false;

  lonMin = i_region.lonMin;
  lonMax = i_region.lonMax;
  latMin = i_region.latMin;
  latMax = i_region.latMax;
  gridW = l_w;
  gridH = l_h;

  // Metres per degree at the region's centre latitude.
  const double l_latC = 0.5 * (i_region.latMin + i_region.latMax);
  const double l_lonC = 0.5 * (i_region.lonMin + i_region.lonMax);
  const double l_mPerLat = 111132.0;
  const double l_mPerLon = 111320.0 * std::cos(glm::radians(l_latC));

  const double l_widthM = (i_region.lonMax - i_region.lonMin) * l_mPerLon;
  const double l_heightM = (i_region.latMax - i_region.latMin) * l_mPerLat;
  const double l_maxM = std::max(1.0, std::max(l_widthM, l_heightM));

  // Normalise so the larger horizontal side spans 200 world units.
  const double l_scaleXZ = 200.0 / l_maxM;
  m_scaleY = (float)l_scaleXZ;
  m_scaleXZ = (float)l_scaleXZ;

  std::vector<float> l_xz((size_t)l_w * l_h * 2);
  for (int j = 0; j < l_h; j++) {
    double l_lat = i_region.latMin + (double)j *
                                         (i_region.latMax - i_region.latMin) /
                                         (double)(l_h - 1);
    for (int i = 0; i < l_w; i++) {
      double l_lon = i_region.lonMin + (double)i *
                                           (i_region.lonMax - i_region.lonMin) /
                                           (double)(l_w - 1);
      double l_x = (l_lon - l_lonC) * l_mPerLon * l_scaleXZ;
      // North (+lat) maps to -Z so north is "away" from a default camera.
      double l_z = -(l_lat - l_latC) * l_mPerLat * l_scaleXZ;
      l_xz[((size_t)j * l_w + i) * 2 + 0] = (float)l_x;
      l_xz[((size_t)j * l_w + i) * 2 + 1] = (float)l_z;
    }
  }

  // World-unit size of one grid cell (the larger side spans 200 units);
  // drives the LOD pick in draw().
  m_cellWorld = 200.0f / (float)std::max(std::max(l_w, l_h) - 1, 1);

  // Grid extents and elevation range, for the frustum culling in draw().
  const size_t l_last = (size_t)l_w * l_h - 1;
  m_x0 = l_xz[0];
  m_x1 = l_xz[l_last * 2 + 0];
  m_z0 = l_xz[1];
  m_z1 = l_xz[l_last * 2 + 1];
  m_minElev = 0.0f;
  m_maxElev = 0.0f;
  for (size_t l_v = 0; l_v < i_region.elev.size(); l_v++) {
    m_minElev = std::min(m_minElev, i_region.elev[l_v]);
    m_maxElev = std::max(m_maxElev, i_region.elev[l_v]);
  }

  // Resize the flat sea overlay to the grid's actual (possibly non-square)
  // extent. init() seeds it as a fixed ±100 square, matching only the larger
  // axis — a non-square region left it overhanging past the real coastline
  // on the shorter axis. That overhang is easy to miss face-on but, viewed
  // at a grazing tilt, its dead-straight edge gets foreshortened into a
  // large, hard-edged patch of flat overlay colour with no terrain under it.
  const float l_quad[8] = {m_x0, m_z1, m_x1, m_z1, m_x0, m_z0, m_x1, m_z0};
  glBindBuffer(GL_ARRAY_BUFFER, m_seaVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(l_quad), l_quad, GL_STATIC_DRAW);

  m_xz = l_xz;
  m_hasDispl = false;
  // A new grid invalidates the overlay until buildSlab2Overlay() rebuilds it.
  m_hasOverlay = false;
  std::vector<float> l_disp((size_t)l_w * l_h, 0.0f);

  if (m_vao == 0) {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbXZ);
    glGenBuffers(1, &m_vbElv);
    glGenBuffers(1, &m_vbDisp);
    glGenBuffers(k_maxLod, m_ebos);
  }

  glBindVertexArray(m_vao);

  glBindBuffer(GL_ARRAY_BUFFER, m_vbXZ);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(l_xz.size() * sizeof(float)),
               l_xz.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, m_vbElv);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)(i_region.elev.size() * sizeof(float)),
               i_region.elev.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(1);

  glBindBuffer(GL_ARRAY_BUFFER, m_vbDisp);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(l_disp.size() * sizeof(float)),
               l_disp.data(), GL_DYNAMIC_DRAW);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(2);

  // One index buffer per LOD level (vertex stride doubles each level, until a
  // further level would no longer reduce the grid).
  m_numLods = 0;
  std::vector<unsigned int> l_idx;
  for (int l_L = 0; l_L < k_maxLod; l_L++) {
    const int l_stride = 1 << l_L;
    if (l_L > 0 && l_stride >= l_w - 1 && l_stride >= l_h - 1)
      break;
    lod::buildIndices(l_w, l_h, l_stride, l_idx);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebos[l_L]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(l_idx.size() * sizeof(unsigned int)),
                 l_idx.data(), GL_STATIC_DRAW);
    m_idxCnts[l_L] = (GLsizei)l_idx.size();
    m_numLods = l_L + 1;
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebos[0]);

  // Resize the sea-level plane to the exact horizontal footprint of this
  // region.
  const float l_seaHalfX = (float)(0.5 * l_widthM * l_scaleXZ);
  const float l_seaHalfZ = (float)(0.5 * l_heightM * l_scaleXZ);
  const float l_sea[8] = {-l_seaHalfX, -l_seaHalfZ, l_seaHalfX, -l_seaHalfZ,
                          -l_seaHalfX, l_seaHalfZ,  l_seaHalfX, l_seaHalfZ};
  glBindBuffer(GL_ARRAY_BUFFER, m_seaVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(l_sea), l_sea, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindVertexArray(0);
  return true;
}

void RegionView::worldToLonLat(float i_worldX,
                               float i_worldZ,
                               double& o_lon,
                               double& o_lat) const {
  const double l_latC = 0.5 * (latMin + latMax);
  const double l_lonC = 0.5 * (lonMin + lonMax);
  const double l_mPerLat = 111132.0;
  const double l_mPerLon = 111320.0 * std::cos(glm::radians(l_latC));
  // Inverse of the lon/lat → world mapping in load() (note world Z = -lat).
  o_lon = l_lonC + ((double)i_worldX / m_scaleXZ) / l_mPerLon;
  o_lat = l_latC - ((double)i_worldZ / m_scaleXZ) / l_mPerLat;
}

void RegionView::lonLatToWorld(double i_lon,
                               double i_lat,
                               float& o_worldX,
                               float& o_worldZ) const {
  const double l_latC = 0.5 * (latMin + latMax);
  const double l_lonC = 0.5 * (lonMin + lonMax);
  const double l_mPerLat = 111132.0;
  const double l_mPerLon = 111320.0 * std::cos(glm::radians(l_latC));
  // Inverse of worldToLonLat above.
  o_worldX = (float)((i_lon - l_lonC) * l_mPerLon * m_scaleXZ);
  o_worldZ = (float)((l_latC - i_lat) * l_mPerLat * m_scaleXZ);
}

void RegionView::computeDisplacementField(
    float i_worldX,
    float i_worldZ,
    const displacement::DisplacementModel& i_model,
    std::vector<float>& o_disp,
    float& o_peak) const {
  const size_t l_n = m_xz.size() / 2;
  o_disp.assign(l_n, 0.0f);
  o_peak = 0.0f;
  for (size_t l_v = 0; l_v < l_n; l_v++) {
    // Local east/north offset from the click point, in metres. World Z runs
    // south to north as -lat, so +north = -dz.
    double l_east = ((double)m_xz[l_v * 2 + 0] - i_worldX) / m_scaleXZ;
    double l_north = -((double)m_xz[l_v * 2 + 1] - i_worldZ) / m_scaleXZ;
    o_disp[l_v] = (float)i_model.verticalDisplacement(l_east, l_north);
    o_peak = std::max(o_peak, std::abs(o_disp[l_v]));
  }
}

void RegionView::applyDisplacementField(const std::vector<float>& i_disp,
                                        float i_peak) {
  if (m_vao == 0 || i_disp.empty() || i_disp.size() != m_xz.size() / 2)
    return;

  glBindBuffer(GL_ARRAY_BUFFER, m_vbDisp);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  (GLsizeiptr)(i_disp.size() * sizeof(float)), i_disp.data());

  // Map the peak to a fixed on-screen height so the field is always visible.
  constexpr float k_targetHeight = 30.0f;
  m_dispPeak = i_peak;
  m_dispScaleY = (i_peak > 0.0f) ? k_targetHeight / i_peak : 0.0f;
  m_hasDispl = true;
}

void RegionView::clearDisplacement() {
  if (m_vao == 0 || m_xz.empty())
    return;
  std::vector<float> l_zero(m_xz.size() / 2, 0.0f);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbDisp);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  (GLsizeiptr)(l_zero.size() * sizeof(float)), l_zero.data());
  m_dispPeak = 0.0f;
  m_dispScaleY = 0.0f;
  m_hasDispl = false;
}

void RegionView::buildSlab2Overlay(const io::Slab2Reader& i_slab2) {
  if (m_vao == 0 || gridW < 2 || gridH < 2)
    return;

  // Slab2 is coarse (0.05°); a fine bathymetry grid would over-sample it and
  // make the texture needlessly large, so cap the overlay resolution per axis.
  constexpr t_idx k_maxDim = 1024;
  const t_idx l_ow = std::min<t_idx>((t_idx)gridW, k_maxDim);
  const t_idx l_oh = std::min<t_idx>((t_idx)gridH, k_maxDim);

  // Orange where a slab exists, fully transparent elsewhere. Row j runs from
  // latMin (row 0) to latMax, matching the quad's texcoords below.
  std::vector<unsigned char> l_rgba((size_t)l_ow * l_oh * 4, 0);
  for (t_idx l_j = 0; l_j < l_oh; l_j++) {
    const double l_lat =
        latMin + (double)l_j * (latMax - latMin) / (double)(l_oh - 1);
    for (t_idx l_i = 0; l_i < l_ow; l_i++) {
      const double l_lon =
          lonMin + (double)l_i * (lonMax - lonMin) / (double)(l_ow - 1);
      const size_t l_k = ((size_t)l_j * l_ow + l_i) * 4;
      if (i_slab2.query(l_lon, l_lat).valid) {
        l_rgba[l_k + 0] = 255;
        l_rgba[l_k + 1] = 120;
        l_rgba[l_k + 2] = 40;
        l_rgba[l_k + 3] = 80;
      }
    }
  }

  if (m_overlayTex == 0)
    glGenTextures(1, &m_overlayTex);
  glBindTexture(GL_TEXTURE_2D, m_overlayTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)l_ow, (GLsizei)l_oh, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, l_rgba.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  // Flat sea-level quad spanning the region's footprint. Corner (x, z) mapping
  // mirrors load(): lonMin → -halfX, latMin → +halfZ (world Z = -lat). Layout
  // per vertex: x, z, u, v.
  const double l_latC = 0.5 * (latMin + latMax);
  const double l_mPerLat = 111132.0;
  const double l_mPerLon = 111320.0 * std::cos(glm::radians(l_latC));
  const float l_halfX =
      (float)(0.5 * (lonMax - lonMin) * l_mPerLon * m_scaleXZ);
  const float l_halfZ =
      (float)(0.5 * (latMax - latMin) * l_mPerLat * m_scaleXZ);
  const float l_quad[16] = {
      -l_halfX, l_halfZ,  0.0f, 0.0f, // lonMin, latMin
      l_halfX,  l_halfZ,  1.0f, 0.0f, // lonMax, latMin
      -l_halfX, -l_halfZ, 0.0f, 1.0f, // lonMin, latMax
      l_halfX,  -l_halfZ, 1.0f, 1.0f, // lonMax, latMax
  };

  if (m_overlayVao == 0) {
    glGenVertexArrays(1, &m_overlayVao);
    glGenBuffers(1, &m_overlayVbo);
  }
  glBindVertexArray(m_overlayVao);
  glBindBuffer(GL_ARRAY_BUFFER, m_overlayVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(l_quad), l_quad, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void*)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  m_hasOverlay = true;
}

void RegionView::beginSimulation(t_idx i_nx, t_idx i_ny, const float* i_bath) {
  if (i_nx < 2 || i_ny < 2 || i_bath == nullptr)
    return;
  m_simNx = i_nx;
  m_simNy = i_ny;

  // Same lon/lat → world mapping as the terrain mesh, so the water aligns.
  const double l_latC = 0.5 * (latMin + latMax);
  const double l_lonC = 0.5 * (lonMin + lonMax);
  const double l_mPerLat = 111132.0;
  const double l_mPerLon = 111320.0 * std::cos(glm::radians(l_latC));

  const size_t l_n = (size_t)i_nx * i_ny;
  std::vector<float> l_xz(l_n * 2);
  m_simBath.assign(i_bath, i_bath + l_n);
  for (t_idx l_j = 0; l_j < i_ny; l_j++) {
    const double l_lat =
        latMin + (double)l_j * (latMax - latMin) / (double)(i_ny - 1);
    for (t_idx l_i = 0; l_i < i_nx; l_i++) {
      const double l_lon =
          lonMin + (double)l_i * (lonMax - lonMin) / (double)(i_nx - 1);
      const double l_x = (l_lon - l_lonC) * l_mPerLon * m_scaleXZ;
      const double l_z = -(l_lat - l_latC) * l_mPerLat * m_scaleXZ;
      const size_t l_k = (size_t)l_j * i_nx + l_i;
      l_xz[l_k * 2 + 0] = (float)l_x;
      l_xz[l_k * 2 + 1] = (float)l_z;
    }
  }

  m_waterCellWorld = 200.0f / (float)std::max((int)std::max(i_nx, i_ny) - 1, 1);
  m_waterH.assign(l_n, 0.0f);

  if (m_waterVao == 0) {
    glGenVertexArrays(1, &m_waterVao);
    glGenBuffers(1, &m_waterVbXZ);
    glGenBuffers(1, &m_waterVbBath);
    glGenBuffers(1, &m_waterVbH);
    glGenBuffers(k_maxLod, m_waterEbos);
  }

  glBindVertexArray(m_waterVao);

  glBindBuffer(GL_ARRAY_BUFFER, m_waterVbXZ);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(l_xz.size() * sizeof(float)),
               l_xz.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, m_waterVbBath);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m_simBath.size() * sizeof(float)),
               m_simBath.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(1);

  glBindBuffer(GL_ARRAY_BUFFER, m_waterVbH);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m_waterH.size() * sizeof(float)),
               m_waterH.data(), GL_DYNAMIC_DRAW);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(2);

  // LOD index buffers for the sim grid, mirroring the terrain mesh setup.
  m_waterNumLods = 0;
  std::vector<unsigned int> l_idx;
  for (int l_L = 0; l_L < k_maxLod; l_L++) {
    const int l_stride = 1 << l_L;
    if (l_L > 0 && l_stride >= (int)i_nx - 1 && l_stride >= (int)i_ny - 1)
      break;
    lod::buildIndices((int)i_nx, (int)i_ny, l_stride, l_idx);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_waterEbos[l_L]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(l_idx.size() * sizeof(unsigned int)),
                 l_idx.data(), GL_STATIC_DRAW);
    m_waterIdxCnts[l_L] = (GLsizei)l_idx.size();
    m_waterNumLods = l_L + 1;
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_waterEbos[0]);

  glBindVertexArray(0);
  // Seed the colour scale from the earthquake displacement: the open-ocean
  // surface anomaly never much exceeds the initial uplift, so this gives a
  // stable reference from the very first frame instead of starting tiny and
  // rescaling upward.
  m_waterAnom = std::max(0.05f, m_dispPeak);
  m_simulating = true;
}

void RegionView::updateWater(const float* i_h) {
  if (!m_simulating || m_waterVao == 0 || i_h == nullptr)
    return;
  const size_t l_n = (size_t)m_simNx * m_simNy;

  float l_peak = 0.0f;
  for (size_t l_k = 0; l_k < l_n; l_k++) {
    m_waterH[l_k] = i_h[l_k];
    // Exclude shallow cells: shoaling produces extreme eta there and would
    // collapse the colour scale for the open-ocean signal.
    const float l_depth = -m_simBath[l_k];
    if (i_h[l_k] >= 0.01f && l_depth > 500.0f)
      l_peak = std::max(l_peak, std::abs(m_simBath[l_k] + i_h[l_k]));
  }
  // Latch the running maximum: the colour scale only ever grows, never
  // shrinks frame-to-frame. A scale that rescaled every frame made identical
  // wave heights flicker between colours as the global peak rose and fell.
  m_waterAnom = std::max(m_waterAnom, l_peak);

  glBindBuffer(GL_ARRAY_BUFFER, m_waterVbH);
  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(l_n * sizeof(float)),
                  m_waterH.data());
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RegionView::draw(const glm::mat4& i_vp, const glm::vec3& i_camPos) const {
  if (m_numLods == 0)
    return;

  // Number of screen-space tiles (per axis) the visible window is split into
  // before picking a LOD level, so each tile's level is based on ITS OWN
  // camera distance rather than one distance for the whole draw call. A
  // single level (from camera orbit distance alone) is fine top-down, where
  // every visible cell is roughly equidistant, but at a grazing/tilted-down
  // angle the near part of the same window can be tens of world units away
  // while the far part is thousands away — one stride is then either far too
  // coarse for the far part (visible as huge flat facets) or far too fine for
  // the near part.
  const int k_lodBands = 4;

  // Frustum-cull off-screen grid rows/columns: zoomed in, the full mesh would
  // put millions of off-screen triangles through the vertex stage every frame.
  // The slab spans the mesh's vertical extent (terrain relief; +40 world
  // units of headroom covers the displacement-preview bumps).
  const float l_scl = m_scaleY * vertExaggeration;
  float l_minX, l_maxX, l_minZ, l_maxZ;
  lod::frustumFootprintXZ(i_vp, m_minElev * l_scl - 1.0f,
                          m_maxElev * l_scl + 40.0f, l_minX, l_maxX, l_minZ,
                          l_maxZ);
  int l_i0, l_i1, l_j0, l_j1;
  lod::visibleRange(m_x0, m_x1, gridW, l_minX, l_maxX, l_i0, l_i1);
  lod::visibleRange(m_z0, m_z1, gridH, l_minZ, l_maxZ, l_j0, l_j1);

  // While a simulation runs, the wave mesh doubles as a gap-free water sheet:
  // its dry cells are snapped to sea level instead of discarded (see
  // region_water.vert/.frag). For that sheet to reliably cover every
  // underwater pixel, the seabed must not write depth — otherwise camera-
  // facing underwater slopes poke in front of the near-sea-level surface and
  // show as bare seabed, and nearly coplanar shelf areas z-fight against it.
  // So split the terrain into a land pass (writes depth, occludes water) and
  // a seabed pass (colour only). The still preview keeps the original single
  // pass (uClip = 0) — it never had this problem.
  const bool l_simView = m_simulating && field == Field::Bathymetry;

  m_terrShader.use();
  m_terrShader.setMat4("uVP", i_vp);
  m_terrShader.setFloat("uScaleY", m_scaleY * vertExaggeration);
  m_terrShader.setFloat("uDispScaleY",
                        m_dispScaleY * (vertExaggeration / 25.0f));
  m_terrShader.setFloat("uDispRange", m_dispPeak);
  m_terrShader.setInt("uMode", field == Field::Displacement ? 1 : 0);
  glBindVertexArray(m_vao);
  auto l_bindTerrLod = [&](int i_level) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebos[i_level]);
  };
  if (l_simView) {
    m_terrShader.setInt("uClip", 1); // land only, writes depth
    lod::drawGridWindowBanded(gridW, gridH, l_i0, l_i1, l_j0, l_j1, m_x0, m_x1,
                              m_z0, m_z1, i_camPos, m_cellWorld, m_numLods,
                              lodViewportPx, k_lodBands, l_bindTerrLod);
    m_terrShader.setInt("uClip", 2); // seabed only, colour without depth write
    glDepthMask(GL_FALSE);
    lod::drawGridWindowBanded(gridW, gridH, l_i0, l_i1, l_j0, l_j1, m_x0, m_x1,
                              m_z0, m_z1, i_camPos, m_cellWorld, m_numLods,
                              lodViewportPx, k_lodBands, l_bindTerrLod);
    glDepthMask(GL_TRUE);
  } else {
    m_terrShader.setInt("uClip", 0);
    lod::drawGridWindowBanded(gridW, gridH, l_i0, l_i1, l_j0, l_j1, m_x0, m_x1,
                              m_z0, m_z1, i_camPos, m_cellWorld, m_numLods,
                              lodViewportPx, k_lodBands, l_bindTerrLod);
  }
  glBindVertexArray(0);

  if (l_simView && m_waterVao) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_waterShader.use();
    m_waterShader.setMat4("uVP", i_vp);
    m_waterShader.setFloat("uScaleY", m_scaleY * vertExaggeration);
    m_waterShader.setFloat("uWaveExagg", waveExaggeration);
    m_waterShader.setFloat("uAnom", m_waterAnom);
    // The water grid spans the same lon/lat box as the terrain, so the same
    // footprint applies — only its vertex counts differ.
    int l_wi0, l_wi1, l_wj0, l_wj1;
    lod::visibleRange(m_x0, m_x1, (int)m_simNx, l_minX, l_maxX, l_wi0, l_wi1);
    lod::visibleRange(m_z0, m_z1, (int)m_simNy, l_minZ, l_maxZ, l_wj0, l_wj1);
    glBindVertexArray(m_waterVao);
    lod::drawGridWindowBanded(
        (int)m_simNx, (int)m_simNy, l_wi0, l_wi1, l_wj0, l_wj1, m_x0, m_x1,
        m_z0, m_z1, i_camPos, m_waterCellWorld, m_waterNumLods, lodViewportPx,
        k_lodBands, [&](int i_level) {
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_waterEbos[i_level]);
        });
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  } else if (showSea && m_seaVao && field == Field::Bathymetry) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_seaShader.use();
    m_seaShader.setMat4("uVP", i_vp);
    glBindVertexArray(m_seaVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  // Subduction-zone overlay: a semi-transparent orange sheet at sea level,
  // drawn last so it reads on top of the terrain/water. GL_LEQUAL lets it sit
  // flush without writing depth; land poking above sea level still occludes it.
  if (showSlab2Overlay && m_hasOverlay && m_overlayVao) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    m_overlayShader.use();
    m_overlayShader.setMat4("uVP", i_vp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_overlayTex);
    m_overlayShader.setInt("u_overlay", 0);
    glBindVertexArray(m_overlayVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }
}

RegionView::~RegionView() {
  if (m_vao) {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbXZ);
    glDeleteBuffers(1, &m_vbElv);
    glDeleteBuffers(1, &m_vbDisp);
    glDeleteBuffers(k_maxLod, m_ebos);
  }
  if (m_seaVao) {
    glDeleteVertexArrays(1, &m_seaVao);
    glDeleteBuffers(1, &m_seaVbo);
  }
  if (m_overlayVao) {
    glDeleteVertexArrays(1, &m_overlayVao);
    glDeleteBuffers(1, &m_overlayVbo);
  }
  if (m_overlayTex)
    glDeleteTextures(1, &m_overlayTex);
  if (m_waterVao) {
    glDeleteVertexArrays(1, &m_waterVao);
    glDeleteBuffers(1, &m_waterVbXZ);
    glDeleteBuffers(1, &m_waterVbBath);
    glDeleteBuffers(1, &m_waterVbH);
    glDeleteBuffers(k_maxLod, m_waterEbos);
  }
}

} // namespace visualization
} // namespace tsunami_lab
