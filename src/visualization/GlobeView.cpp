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
    glDeleteBuffers(1, &m_selFillEbo);
    glDeleteBuffers(1, &m_selOutlineEbo);
  }
  if (m_slabVao) {
    glDeleteVertexArrays(1, &m_slabVao);
    glDeleteBuffers(1, &m_slabVbo);
    glDeleteBuffers(1, &m_slabEbo);
  }
  if (m_slabTex)
    glDeleteTextures(1, &m_slabTex);
}

// Inline shaders for the subduction-zone overlay: a tessellated sphere mesh
// in (lon, lat), textured with the depth-graded Slab2 coverage.
static const char* k_slabVert = R"(#version 330 core
layout(location = 0) in vec2 aPos; // (lon, lat) in degrees
uniform mat4 uVP;
uniform float uRadius;
out vec2 vUV;
void main() {
    vUV = vec2((aPos.x + 180.0) / 360.0, (aPos.y + 90.0) / 180.0);
    float lon = radians(aPos.x);
    float lat = radians(aPos.y);
    float cl = cos(lat);
    vec3 dir = vec3(cl * sin(lon), sin(lat), cl * cos(lon));
    // Slightly above the terrain radius, below the selection mesh.
    gl_Position = uVP * vec4(dir * (uRadius + 0.08), 1.0);
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

  // Tessellated world grid in (lon, lat); the vertex shader maps each vertex
  // onto the sphere and derives the UV. A single flat quad's 4 corners would
  // degenerate to the poles once mapped onto a sphere, so this needs real
  // subdivision — 2° spacing keeps the curvature smooth at a trivial
  // triangle budget (~65 k triangles, built once).
  const int l_gw = 181; // 2° steps over 360°
  const int l_gh = 91;  // 2° steps over 180°
  std::vector<float> l_verts((size_t)l_gw * l_gh * 2);
  for (int l_j = 0; l_j < l_gh; l_j++) {
    const float l_lat = -90.0f + l_j * (180.0f / (l_gh - 1));
    for (int l_i = 0; l_i < l_gw; l_i++) {
      const float l_lon = -180.0f + l_i * (360.0f / (l_gw - 1));
      l_verts[((size_t)l_j * l_gw + l_i) * 2 + 0] = l_lon;
      l_verts[((size_t)l_j * l_gw + l_i) * 2 + 1] = l_lat;
    }
  }
  std::vector<unsigned int> l_idx;
  lod::buildIndices(l_gw, l_gh, 1, l_idx);

  if (m_slabVao == 0) {
    glGenVertexArrays(1, &m_slabVao);
    glGenBuffers(1, &m_slabVbo);
    glGenBuffers(1, &m_slabEbo);
  }
  glBindVertexArray(m_slabVao);
  glBindBuffer(GL_ARRAY_BUFFER, m_slabVbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(l_verts.size() * sizeof(float)),
               l_verts.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_slabEbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               (GLsizeiptr)(l_idx.size() * sizeof(unsigned int)),
               l_idx.data(), GL_STATIC_DRAW);
  m_slabIdxCnt = (GLsizei)l_idx.size();
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
  glGenBuffers(1, &m_selFillEbo);
  glGenBuffers(1, &m_selOutlineEbo);

  glBindVertexArray(m_selVao);
  glBindBuffer(GL_ARRAY_BUFFER, m_selVbo);
  // kSelGridN x kSelGridN vertices x 2 floats; dynamic, re-uploaded whenever
  // the selection bounds change (uploadSelectionRect).
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)((size_t)kSelGridN * kSelGridN * 2 * sizeof(float)),
               nullptr, GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);

  // Grid topology never changes (only the vertex positions do), so the fill
  // and outline index buffers are built once here.
  std::vector<unsigned int> l_fillIdx;
  lod::buildIndices(kSelGridN, kSelGridN, 1, l_fillIdx);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_selFillEbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               (GLsizeiptr)(l_fillIdx.size() * sizeof(unsigned int)),
               l_fillIdx.data(), GL_STATIC_DRAW);

  // Outline: perimeter of the grid, traced top row (W->E), right column
  // (N->S), bottom row (E->W), left column (S->N), closing the loop.
  std::vector<unsigned int> l_outlineIdx;
  for (int l_i = 0; l_i < kSelGridN; l_i++) // top row (j=0), W->E
    l_outlineIdx.push_back((unsigned int)l_i);
  for (int l_j = 1; l_j < kSelGridN; l_j++) // right column, N->S
    l_outlineIdx.push_back((unsigned int)(l_j * kSelGridN + (kSelGridN - 1)));
  for (int l_i = kSelGridN - 2; l_i >= 0; l_i--) // bottom row, E->W
    l_outlineIdx.push_back((unsigned int)((kSelGridN - 1) * kSelGridN + l_i));
  for (int l_j = kSelGridN - 2; l_j >= 0; l_j--) // left column, S->N
    l_outlineIdx.push_back((unsigned int)(l_j * kSelGridN));
  m_selOutlineCnt = (GLsizei)l_outlineIdx.size();
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_selOutlineEbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               (GLsizeiptr)(l_outlineIdx.size() * sizeof(unsigned int)),
               l_outlineIdx.data(), GL_STATIC_DRAW);

  glBindVertexArray(0);
}

void GlobeView::draw(const glm::mat4& i_vp) const {
  if (m_terrVao && m_terrNumLods > 0 && m_terrMinLod >= 0) {
    // Never pick a level whose index buffer was skipped/failed at build time.
    // There is no more frustum-footprint windowing (that relied on the flat
    // map's y=0 plane): the whole sphere mesh is submitted in one draw call,
    // so a LOD floor bounds the worst case (kMinLod=2 -> at most ~4.7 M
    // triangles for the default 8640-sample grid) regardless of zoom.
    constexpr int kMinLod = 2;
    const int l_lod = std::max(
        {m_terrMinLod, kMinLod,
         lod::pickLevel(m_cellWorld, m_terrNumLods, lodCamDistance,
                        lodViewportPx)});
    m_terrShader.use();
    m_terrShader.setMat4("uVP", i_vp);
    m_terrShader.setFloat("uRadius", RADIUS);
    glBindVertexArray(m_terrVao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrEbos[l_lod]);
    glDrawElements(GL_TRIANGLES, m_terrIdxCnts[l_lod], GL_UNSIGNED_INT,
                   nullptr);
    glBindVertexArray(0);
  }

  // Subduction zones: a depth-graded, semi-transparent sheet over the
  // sphere, under the selection mesh.
  if (showSlab2Overlay && m_hasSlabOverlay && m_slabVao) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_slabShader.use();
    m_slabShader.setMat4("uVP", i_vp);
    m_slabShader.setFloat("uRadius", RADIUS);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_slabTex);
    m_slabShader.setInt("uSlab", 0);
    glBindVertexArray(m_slabVao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_slabEbo);
    glDrawElements(GL_TRIANGLES, m_slabIdxCnt, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
  }

  if ((m_selecting || m_hasSelection) && m_selVao) {
    uploadSelectionRect();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_selShader.use();
    m_selShader.setMat4("uVP", i_vp);
    m_selShader.setFloat("uRadius", RADIUS);

    glBindVertexArray(m_selVao);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_selFillEbo);
    m_selShader.setVec4("uColor", glm::vec4(1.0f, 0.85f, 0.1f, 0.22f));
    glDrawElements(GL_TRIANGLES, kSelSubdiv * kSelSubdiv * 6, GL_UNSIGNED_INT,
                   nullptr);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_selOutlineEbo);
    glLineWidth(2.0f);
    m_selShader.setVec4("uColor", glm::vec4(1.0f, 0.9f, 0.0f, 1.0f));
    glDrawElements(GL_LINE_LOOP, m_selOutlineCnt, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    glDisable(GL_BLEND);
  }
}

void GlobeView::uploadSelectionRect() const {
  const float lonMin = std::min(m_selA.x, m_selB.x);
  const float lonMax = std::max(m_selA.x, m_selB.x);
  const float latMin = std::min(m_selA.y, m_selB.y);
  const float latMax = std::max(m_selA.y, m_selB.y);

  // Tessellate the rectangle into a kSelGridN x kSelGridN grid so the fill
  // and outline follow the sphere's curvature (see initSelectionVao for the
  // fixed index topology this fills).
  std::vector<float> verts((size_t)kSelGridN * kSelGridN * 2);
  for (int j = 0; j < kSelGridN; j++) {
    const float lat =
        latMin + (latMax - latMin) * (float)j / (float)kSelSubdiv;
    for (int i = 0; i < kSelGridN; i++) {
      const float lon =
          lonMin + (lonMax - lonMin) * (float)i / (float)kSelSubdiv;
      verts[((size_t)j * kSelGridN + i) * 2 + 0] = lon;
      verts[((size_t)j * kSelGridN + i) * 2 + 1] = lat;
    }
  }
  glBindBuffer(GL_ARRAY_BUFFER, m_selVbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  (GLsizeiptr)(verts.size() * sizeof(float)), verts.data());
}

bool GlobeView::screenToLonLat(float i_mx,
                               float i_my,
                               int i_w,
                               int i_h,
                               const Camera& i_cam,
                               float& o_lon,
                               float& o_lat) const {
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

  // Ray-sphere intersection against the globe (centred at the origin).
  const float b = 2.0f * glm::dot(orig, dir);
  const float c = glm::dot(orig, orig) - RADIUS * RADIUS;
  const float disc = b * b - 4.0f * c;
  if (disc < 0.0f)
    return false; // ray misses the sphere entirely

  const float sq = std::sqrt(disc);
  float t = (-b - sq) * 0.5f; // nearest intersection
  if (t < 0.0f)
    t = (-b + sq) * 0.5f;
  if (t < 0.0f)
    return false; // sphere is behind the ray origin

  const glm::vec3 hit = orig + t * dir;
  o_lat = glm::degrees(std::asin(std::max(-1.0f, std::min(1.0f, hit.y / RADIUS))));
  o_lon = glm::degrees(std::atan2(hit.x, hit.z));
  return true;
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
  float lon, lat;
  if (!screenToLonLat(i_mx, i_my, i_w, i_h, i_cam, lon, lat)) {
    m_selecting = false; // pressed off the visible limb of the globe
    return;
  }
  m_selA = {lon, lat};
  m_selB = {lon, lat};
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
  float lon, lat;
  // A miss (dragged past the globe's limb) keeps the last valid endpoint
  // rather than snapping the selection to an undefined position.
  if (!screenToLonLat(i_mx, i_my, i_w, i_h, i_cam, lon, lat))
    return;
  m_selB = clampSelEnd({lon, lat});
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
