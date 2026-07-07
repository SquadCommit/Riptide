#ifndef TSUNAMI_LAB_VISUALIZATION_GLOBEVIEW_H
#define TSUNAMI_LAB_VISUALIZATION_GLOBEVIEW_H

#include "BBox.h"
#include "Camera.h"
#include "Lod.h"
#include "Shader.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

namespace tsunami_lab {
namespace io {
class Slab2Reader;
} // namespace io
namespace visualization {

class GlobeView {
public:
  static constexpr float DEFAULT_MAX_SEL_DEG = 20.0f;

  // Full GEBCO 2025 is ~3.7 billion points, so native resolution is not an
  // option.
  static constexpr int DEFAULT_LON_SAMPLES = 8640; // ~0.042° (2.5 arc-min)
  static constexpr int MIN_LON_SAMPLES = 360;      // 1.0°
  static constexpr int MAX_LON_SAMPLES = 17280;    // ~0.021° (1.25 arc-min)

  void init(const char* i_gebcoPath);
  void setResolution(int i_lonSamples);
  int resolution() const { return m_lonSamples; }
  void draw(const glm::mat4& i_vp) const;

  /**
   * Builds the global subduction-zone overlay: samples the Slab2 model onto an
   * equirectangular RGBA texture, colour-graded by slab depth. One-time cost;
   * call once after init() when a Slab2 reader is available.
   *
   * @param i_slab2 Slab2 reader sampled per texel.
   **/
  void buildSlab2Overlay(const io::Slab2Reader& i_slab2);

  void
  onMousePress(float i_mx, float i_my, int i_w, int i_h, const Camera& i_cam);
  void onMouseRelease();
  void
  onMouseMove(float i_mx, float i_my, int i_w, int i_h, const Camera& i_cam);

  bool hasSelection() const { return m_hasSelection; }
  BBox getSelection() const;
  void setSelection(const BBox& i_bbox);
  void clearSelection() {
    m_hasSelection = false;
    m_selecting = false;
  }

  float maxSelDeg = DEFAULT_MAX_SEL_DEG;

  bool showSlab2Overlay = true; // draw the subduction-zone overlay pass

  // Per-frame LOD hints, set by the main loop before draw(): camera orbit
  // distance (world units) and framebuffer height (pixels). draw() picks the
  // index-buffer level that keeps triangles at roughly pixel size — the full
  // globe mesh is up to ~150 M sub-pixel triangles otherwise, which multiplies
  // the MSAA fill cost into unusable frame rates.
  float lodCamDistance = 300.0f;
  int lodViewportPx = 720;

  ~GlobeView();

private:
  glm::vec2 unproject(
      float i_mx, float i_my, int i_w, int i_h, const Camera& i_cam) const;
  glm::vec2 clampSelEnd(glm::vec2 i_end) const;
  void uploadSelectionRect() const;
  void loadGebco(const char* i_path, int i_lonSamples);
  void buildTerrainMesh(const float* i_elev, int i_w, int i_h);
  void initSelectionVao();

  Shader m_terrShader;
  Shader m_selShader;

  GLuint m_terrVao = 0;
  GLuint m_terrVbPos = 0;
  GLuint m_terrVbElv = 0;
  // Index buffers at vertex stride 1, 2, 4, … over the same vertex data.
  GLuint m_terrEbos[lod::k_maxLevels] = {};
  GLsizei m_terrIdxCnts[lod::k_maxLevels] = {};
  int m_terrNumLods = 0;
  float m_cellWorld = 0.0f; // world-unit (degree) size of one grid cell
  int m_gridW = 0, m_gridH = 0;

  GLuint m_selVao = 0;
  GLuint m_selVbo = 0;

  // Subduction-zone overlay: a depth-graded RGBA texture on a world-spanning
  // quad (see buildSlab2Overlay).
  Shader m_slabShader;
  GLuint m_slabVao = 0;
  GLuint m_slabVbo = 0;
  GLuint m_slabTex = 0;
  bool m_hasSlabOverlay = false;

  std::string m_gebcoPath;
  int m_lonSamples = DEFAULT_LON_SAMPLES;

  bool m_selecting = false;
  bool m_hasSelection = false;
  glm::vec2 m_selA{0, 0}; // anchor (lon, lat) set on mouse press
  glm::vec2 m_selB{0, 0}; // current drag end (clamped)
};

} // namespace visualization
} // namespace tsunami_lab

#endif
