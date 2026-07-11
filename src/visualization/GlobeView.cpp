#include "GlobeView.h"

#include "Gebco.h"
#include "io/Slab2Reader.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace tsunami_lab {
namespace visualization {

void GlobeView::init(const char* i_gebcoPath) {
  m_terrShader.buildFromFiles(SHADER_DIR "/globe_terrain.vert",
                              SHADER_DIR "/globe_terrain.frag");
  m_selShader.buildFromFiles(SHADER_DIR "/globe_selection.vert",
                             SHADER_DIR "/globe_selection.frag");
  initSelectionVao();
  if (i_gebcoPath) {
    m_gebcoPath = i_gebcoPath;
    loadGebco(m_gebcoPath.c_str(), m_lonSamples);
  }
}

void GlobeView::setResolution(int i_lonSamples) {
  i_lonSamples =
      std::max(MIN_LON_SAMPLES, std::min(MAX_LON_SAMPLES, i_lonSamples));
  if (i_lonSamples == m_lonSamples || m_gebcoPath.empty())
    return;
  m_lonSamples = i_lonSamples;
  loadGebco(m_gebcoPath.c_str(), m_lonSamples);
}

GlobeView::~GlobeView() {
  if (m_terrVao) {
    glDeleteVertexArrays(1, &m_terrVao);
    glDeleteBuffers(1, &m_terrVbPos);
    glDeleteBuffers(1, &m_terrVbElv);
    glDeleteBuffers(lod::k_maxLevels, m_terrEbos);
  }
  if (m_selVao) {
    glDeleteVertexArrays(1, &m_selVao);
    glDeleteBuffers(1, &m_selVbo);
    glDeleteBuffers(1, &m_selEbo);
  }
  if (m_slabVao) {
    glDeleteVertexArrays(1, &m_slabVao);
    glDeleteBuffers(1, &m_slabVbo);
  }
  if (m_slabTex)
    glDeleteTextures(1, &m_slabTex);
}

// Inline shaders for the subduction-zone overlay: a world-spanning quad in
// (lon, lat) over the flat map, textured with the depth-graded Slab2 coverage.
static const char* k_slabVert = R"(#version 330 core
layout(location = 0) in vec2 aPos; // (lon, lat) in degrees
uniform mat4 uVP;
out vec2 vUV;
void main() {
    vUV = vec2((aPos.x + 180.0) / 360.0, (aPos.y + 90.0) / 180.0);
    // Slightly above the y=0 terrain, below the selection quad (y=0.05).
    gl_Position = uVP * vec4(aPos.x, 0.02, -aPos.y, 1.0);
}
)";

static const char* k_slabFrag = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSlab;
out vec4 fragColor;
void main() {
    fragColor = texture(uSlab, vUV);
}
)";

// Depth-graded colour for the subduction-zone overlay: amber at the trench
// (shallow, tsunami-relevant) through orange-red and magenta to violet where
// the slab dives deepest. The sqrt warp spends more of the gradient on the
// shallow part. Mirrored by the legend in drawGlobeUi (main_viz.cpp).
static void slabDepthColor(double i_depthKm, unsigned char* o_rgba) {
  struct Stop {
    float t, r, g, b, a;
  };
  static const Stop k_stops[] = {
      {0.00f, 255.0f, 195.0f, 60.0f, 200.0f},
      {0.35f, 255.0f, 95.0f, 40.0f, 185.0f},
      {0.70f, 205.0f, 45.0f, 115.0f, 150.0f},
      {1.00f, 115.0f, 35.0f, 165.0f, 115.0f},
  };
  const float l_t =
      (float)std::sqrt(std::min(std::max(i_depthKm / 660.0, 0.0), 1.0));
  int l_i = 0;
  while (l_i < 2 && l_t > k_stops[l_i + 1].t)
    l_i++;
  const Stop& l_a = k_stops[l_i];
  const Stop& l_b = k_stops[l_i + 1];
  const float l_f = (l_t - l_a.t) / (l_b.t - l_a.t);
  o_rgba[0] = (unsigned char)(l_a.r + (l_b.r - l_a.r) * l_f);
  o_rgba[1] = (unsigned char)(l_a.g + (l_b.g - l_a.g) * l_f);
  o_rgba[2] = (unsigned char)(l_a.b + (l_b.b - l_a.b) * l_f);
  o_rgba[3] = (unsigned char)(l_a.a + (l_b.a - l_a.a) * l_f);
}

void GlobeView::buildSlab2Overlay(const io::Slab2Reader& i_slab2) {
  // Slab2 grids are 0.05°-spaced; 0.1° texels keep this one-time scan fast
  // while the zones (several degrees across) stay crisp under linear
  // filtering.
  const int l_w = 3600;
  const int l_h = 1800;
  std::vector<unsigned char> l_rgba((size_t)l_w * l_h * 4, 0);
  for (int l_j = 0; l_j < l_h; l_j++) {
    const double l_lat = -90.0 + (l_j + 0.5) * (180.0 / l_h);
    for (int l_i = 0; l_i < l_w; l_i++) {
      const double l_lon = -180.0 + (l_i + 0.5) * (360.0 / l_w);
      const io::Slab2Point l_p = i_slab2.query(l_lon, l_lat);
      if (l_p.valid)
        slabDepthColor(l_p.depth / 1000.0,
                       &l_rgba[((size_t)l_j * l_w + l_i) * 4]);
    }
  }

  m_slabShader.build(k_slabVert, k_slabFrag);

  if (m_slabTex == 0)
    glGenTextures(1, &m_slabTex);
  glBindTexture(GL_TEXTURE_2D, m_slabTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, l_w, l_h, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, l_rgba.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  // World-spanning quad in (lon, lat); the vertex shader derives the UVs.
  const float l_quad[8] = {-180.0f, -90.0f, 180.0f, -90.0f,
                           -180.0f, 90.0f,  180.0f, 90.0f};
  if (m_slabVao == 0) {
    glGenVertexArrays(1, &m_slabVao);
    glGenBuffers(1, &m_slabVbo);
  }
  glBindVertexArray(m_slabVao);
  glBindBuffer(GL_ARRAY_BUFFER, m_slabVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(l_quad), l_quad, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);

  m_hasSlabOverlay = true;
}

void GlobeView::loadGebco(const char* i_path, int i_lonSamples) {
  // Globally subsample the elevation source so the longitude axis holds
  // ~i_lonSamples points; the latitude axis uses the same stride (half as
  // many points over 180 degrees). readRegion() strides by max(axis), which
  // for the 2:1 world box is exactly the longitude axis.
  BBox l_world;
  l_world.lonMin = -180.0f;
  l_world.lonMax = 180.0f;
  l_world.latMin = -90.0f;
  l_world.latMax = 90.0f;

  gebco::Region l_reg;
  if (!gebco::readRegion(i_path, l_world, l_reg, i_lonSamples) || l_reg.w < 2 ||
      l_reg.h < 2) {
    std::fprintf(stderr,
                 "GlobeView: Welt-Gitter konnte nicht gelesen werden\n");
    return;
  }

  std::fprintf(stderr, "[GEBCO] Welt-Gitter: %d\u00d7%d.\n", l_reg.w, l_reg.h);
  buildTerrainMesh(l_reg.elev.data(), l_reg.w, l_reg.h);
}

void GlobeView::buildTerrainMesh(const float* i_elev, int i_w, int i_h) {
  const int nVerts = i_w * i_h;
  std::vector<float> pos((size_t)nVerts * 2);
  for (int j = 0; j < i_h; j++) {
    float lat = -90.0f + j * (180.0f / (i_h - 1));
    for (int i = 0; i < i_w; i++) {
      float lon = -180.0f + i * (360.0f / (i_w - 1));
      pos[(j * i_w + i) * 2 + 0] = lon;
      pos[(j * i_w + i) * 2 + 1] = lat;
    }
  }

  // World-unit (degree) size of one lon cell; drives the LOD pick in draw().
  m_cellWorld = 360.0f / (float)std::max(i_w - 1, 1);
  m_gridW = i_w;
  m_gridH = i_h;

  // Free any previously built mesh so setResolution() can rebuild without leak.
  if (m_terrVao) {
    glDeleteVertexArrays(1, &m_terrVao);
    glDeleteBuffers(1, &m_terrVbPos);
    glDeleteBuffers(1, &m_terrVbElv);
    glDeleteBuffers(lod::k_maxLevels, m_terrEbos);
    m_terrVao = m_terrVbPos = m_terrVbElv = 0;
    for (int l_L = 0; l_L < lod::k_maxLevels; l_L++)
      m_terrEbos[l_L] = 0;
  }

  glGenVertexArrays(1, &m_terrVao);
  glGenBuffers(1, &m_terrVbPos);
  glGenBuffers(1, &m_terrVbElv);
  glGenBuffers(lod::k_maxLevels, m_terrEbos);

  glBindVertexArray(m_terrVao);

  glBindBuffer(GL_ARRAY_BUFFER, m_terrVbPos);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pos.size() * sizeof(float)),
               pos.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, m_terrVbElv);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nVerts * sizeof(float)), i_elev,
               GL_STATIC_DRAW);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(1);

  // One index buffer per LOD level (vertex stride doubles each level, until a
  // further level would no longer reduce the grid). Browsers reject very
  // large buffer uploads (the stride-1 level of a 4320-wide grid is ~224 MB),
  // so levels above the budget are skipped and draw() clamps to the finest
  // level that actually has a buffer — visually equivalent at globe zoom.
  constexpr size_t k_maxEboBytes = 64u << 20;
  m_terrNumLods = 0;
  m_terrMinLod = -1;
  std::vector<unsigned int> idx;
  for (int l_L = 0; l_L < lod::k_maxLevels; l_L++) {
    const int l_stride = 1 << l_L;
    if (l_L > 0 && l_stride >= i_w - 1 && l_stride >= i_h - 1)
      break;
    m_terrIdxCnts[l_L] = 0;
    m_terrNumLods = l_L + 1;
    lod::buildIndices(i_w, i_h, l_stride, idx);
    const size_t l_bytes = idx.size() * sizeof(unsigned int);
    if (l_bytes > k_maxEboBytes) {
      std::fprintf(
          stderr,
          "GlobeView: LOD %d uebersteigt EBO-Budget (%zu MB), ausgelassen\n",
          l_L, l_bytes >> 20);
      continue;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrEbos[l_L]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)l_bytes, idx.data(),
                 GL_STATIC_DRAW);
    const GLenum l_err = glGetError();
    if (l_err != GL_NO_ERROR) {
      std::fprintf(
          stderr,
          "GlobeView: EBO-Upload LOD %d (%zu MB) fehlgeschlagen (0x%x)\n", l_L,
          l_bytes >> 20, l_err);
      continue;
    }
    m_terrIdxCnts[l_L] = (GLsizei)idx.size();
    if (m_terrMinLod < 0)
      m_terrMinLod = l_L;
  }

  glBindVertexArray(0);
}

void GlobeView::initSelectionVao() {
  glGenVertexArrays(1, &m_selVao);
  glGenBuffers(1, &m_selVbo);

  glBindVertexArray(m_selVao);
  glBindBuffer(GL_ARRAY_BUFFER, m_selVbo);
  // 4 vertices × 2 floats, dynamic (updated every frame during selection)
  glBufferData(GL_ARRAY_BUFFER, 4 * 2 * sizeof(float), nullptr,
               GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);
  // Outline order SW→SE→NE→NW→SW over the strip-ordered corners; WebGL2
  // has no client-side index arrays, so the indices live in a small EBO.
  const unsigned int l_lineIdx[5] = {0, 1, 3, 2, 0};
  glGenBuffers(1, &m_selEbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_selEbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(l_lineIdx), l_lineIdx,
               GL_STATIC_DRAW);
  glBindVertexArray(0);
}

void GlobeView::draw(const glm::mat4& i_vp) const {
  if (m_terrVao && m_terrNumLods > 0 && m_terrMinLod >= 0) {
    // Never pick a level whose index buffer was skipped/failed at build time.
    const int l_lod =
        std::max(m_terrMinLod, lod::pickLevel(m_cellWorld, m_terrNumLods,
                                              lodCamDistance, lodViewportPx));
    // The flat map lies in the y = 0 plane; cull grid rows/columns outside
    // the frustum footprint so close zooms do not pay vertex costs for the
    // whole (up to ~150 M triangle) globe mesh. World X = lon, Z = -lat.
    float l_minX, l_maxX, l_minZ, l_maxZ;
    lod::frustumFootprintXZ(i_vp, 0.0f, 0.0f, l_minX, l_maxX, l_minZ, l_maxZ);
    int l_i0, l_i1, l_j0, l_j1;
    lod::visibleRange(-180.0f, 180.0f, m_gridW, l_minX, l_maxX, l_i0, l_i1);
    lod::visibleRange(90.0f, -90.0f, m_gridH, l_minZ, l_maxZ, l_j0, l_j1);
    m_terrShader.use();
    m_terrShader.setMat4("uVP", i_vp);
    glBindVertexArray(m_terrVao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrEbos[l_lod]);
    lod::drawGridWindow(m_gridW, m_gridH, 1 << l_lod, l_i0, l_i1, l_j0, l_j1);
    glBindVertexArray(0);
  }

  // Subduction zones: a depth-graded, semi-transparent sheet over the flat
  // map, under the selection rectangle.
  if (showSlab2Overlay && m_hasSlabOverlay && m_slabVao) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    m_slabShader.use();
    m_slabShader.setMat4("uVP", i_vp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_slabTex);
    m_slabShader.setInt("uSlab", 0);
    glBindVertexArray(m_slabVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
  }

  if ((m_selecting || m_hasSelection) && m_selVao) {
    uploadSelectionRect();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    m_selShader.use();
    m_selShader.setMat4("uVP", i_vp);

    glBindVertexArray(m_selVao);

    m_selShader.setVec4("uColor", glm::vec4(1.0f, 0.85f, 0.1f, 0.22f));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glLineWidth(2.0f);
    m_selShader.setVec4("uColor", glm::vec4(1.0f, 0.9f, 0.0f, 1.0f));
    glDrawElements(GL_LINE_STRIP, 5, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
  }
}

void GlobeView::uploadSelectionRect() const {
  float lonMin = std::min(m_selA.x, m_selB.x);
  float lonMax = std::max(m_selA.x, m_selB.x);
  float latMin = std::min(m_selA.y, m_selB.y);
  float latMax = std::max(m_selA.y, m_selB.y);

  // Corners stored for GL_TRIANGLE_STRIP: SW, SE, NW, NE
  float verts[8] = {
      lonMin, latMin, // SW
      lonMax, latMin, // SE
      lonMin, latMax, // NW
      lonMax, latMax, // NE
  };
  glBindBuffer(GL_ARRAY_BUFFER, m_selVbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
}

glm::vec2 GlobeView::unproject(
    float i_mx, float i_my, int i_w, int i_h, const Camera& i_cam) const {
  float aspect = (i_h > 0) ? (float)i_w / (float)i_h : 1.0f;
  float ndcX = (2.0f * i_mx / (float)i_w) - 1.0f;
  float ndcY = 1.0f - (2.0f * i_my / (float)i_h);

  glm::mat4 invVP = glm::inverse(i_cam.projection(aspect) * i_cam.view());

  glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farP = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearP /= nearP.w;
  farP /= farP.w;

  glm::vec3 dir = glm::normalize(glm::vec3(farP) - glm::vec3(nearP));
  glm::vec3 orig = glm::vec3(nearP);

  if (std::abs(dir.y) < 1e-6f)
    return {0.0f, 0.0f};

  float t = -orig.y / dir.y;
  glm::vec3 world = orig + t * dir;
  // Z = -lat in world space (see vertex shader)
  return {world.x, -world.z};
}

glm::vec2 GlobeView::clampSelEnd(glm::vec2 i_end) const {
  auto clampAxis = [&](float anchor, float end, float maxHalf) {
    float diff = end - anchor;
    if (diff > maxHalf)
      diff = maxHalf;
    if (diff < -maxHalf)
      diff = -maxHalf;
    return anchor + diff;
  };

  float lon = clampAxis(m_selA.x, i_end.x, maxSelDeg);
  float lat = clampAxis(m_selA.y, i_end.y, maxSelDeg);

  lon = std::min(180.0f, std::max(-180.0f, lon));
  lat = std::min(90.0f, std::max(-90.0f, lat));
  return {lon, lat};
}

void GlobeView::onMousePress(
    float i_mx, float i_my, int i_w, int i_h, const Camera& i_cam) {
  glm::vec2 world = unproject(i_mx, i_my, i_w, i_h, i_cam);
  m_selA = world;
  m_selB = world;
  m_selecting = true;
  m_hasSelection = false;
}

void GlobeView::onMouseRelease() {
  if (m_selecting) {
    m_selecting = false;
    BBox sel = getSelection();
    m_hasSelection =
        sel.valid() && sel.lonSpan() > 0.1f && sel.latSpan() > 0.1f;
  }
}

void GlobeView::onMouseMove(
    float i_mx, float i_my, int i_w, int i_h, const Camera& i_cam) {
  if (!m_selecting)
    return;
  glm::vec2 world = unproject(i_mx, i_my, i_w, i_h, i_cam);
  m_selB = clampSelEnd(world);
}

BBox GlobeView::getSelection() const {
  BBox b;
  b.lonMin = std::min(m_selA.x, m_selB.x);
  b.lonMax = std::max(m_selA.x, m_selB.x);
  b.latMin = std::min(m_selA.y, m_selB.y);
  b.latMax = std::max(m_selA.y, m_selB.y);
  return b;
}

void GlobeView::setSelection(const BBox& i_bbox) {
  m_selA = {i_bbox.lonMin, i_bbox.latMin};
  m_selB = {i_bbox.lonMax, i_bbox.latMax};
  m_hasSelection = true;
  m_selecting = false;
  uploadSelectionRect();
}

} // namespace visualization
} // namespace tsunami_lab
