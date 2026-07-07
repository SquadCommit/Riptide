#ifndef TSUNAMI_LAB_VISUALIZATION_REGIONVIEW_H
#define TSUNAMI_LAB_VISUALIZATION_REGIONVIEW_H

#include "../constants.h"
#include "Gebco.h"
#include "Lod.h"
#include "Shader.h"
#include "displacement/DisplacementModel.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

namespace tsunami_lab {
namespace io {
class Slab2Reader;
} // namespace io
namespace visualization {

class RegionView {
public:
  enum class Field { Bathymetry, Displacement };

  void init();
  bool load(const gebco::Region& i_region);
  void draw(const glm::mat4& i_vp, const glm::vec3& i_camPos) const;

  bool loaded() const { return m_numLods > 0; }

  /**
   * CPU half of the displacement build: samples the model over the terrain
   * grid. Touches no GL state, so it may run on a worker thread — as long as
   * no concurrent load() swaps the grid underneath it.
   *
   * @param i_worldX fault centre in the region world frame.
   * @param i_worldZ fault centre in the region world frame.
   * @param i_model displacement model to sample.
   * @param o_disp per-vertex vertical displacement (metres).
   * @param o_peak largest absolute displacement in o_disp.
   **/
  void computeDisplacementField(float i_worldX,
                                float i_worldZ,
                                const displacement::DisplacementModel& i_model,
                                std::vector<float>& o_disp,
                                float& o_peak) const;

  /**
   * GL half: uploads a field produced by computeDisplacementField. Must run
   * on the GL thread; ignored if the grid size changed in the meantime.
   **/
  void applyDisplacementField(const std::vector<float>& i_disp, float i_peak);

  void clearDisplacement();
  bool hasDisplacement() const { return m_hasDispl; }

  /**
   * Builds the semi-transparent subduction-zone overlay by sampling the Slab2
   * model over the loaded region's lon/lat grid into an RGBA texture on a flat
   * sea-level quad. Must be called after load().
   *
   * @param i_slab2 Slab2 reader used to classify each grid cell.
   **/
  void buildSlab2Overlay(const io::Slab2Reader& i_slab2);

  void beginSimulation(t_idx i_nx, t_idx i_ny, const float* i_bath);
  void updateWater(const float* i_h);
  void endSimulation() { m_simulating = false; }
  bool simulating() const { return m_simulating; }
  // Current colour-scale reference: |surface anomaly| mapped to the warm end.
  float waterAnom() const { return m_waterAnom; }

  void worldToLonLat(float i_worldX,
                     float i_worldZ,
                     double& o_lon,
                     double& o_lat) const;

  // Per-frame LOD hint, set by the main loop before draw(): framebuffer
  // height in pixels. draw() combines it with each screen-space tile's own
  // camera distance to pick an index-buffer level that keeps triangles at
  // roughly pixel size — zoomed out, sub-pixel triangles would otherwise
  // multiply the MSAA cost into unusable frame rates without adding any
  // visible detail.
  int lodViewportPx = 720;

  float vertExaggeration = 25.0f;
  float waveExaggeration = 50.0f;
  bool showSea = true;
  bool showSlab2Overlay = true; // draw the subduction-zone overlay pass
  Field field = Field::Bathymetry;

  double lonMin = 0, lonMax = 0, latMin = 0, latMax = 0;
  int gridW = 0, gridH = 0;

  ~RegionView();

private:
  Shader m_terrShader;
  Shader m_seaShader;

  GLuint m_vao = 0;
  GLuint m_vbXZ = 0;
  GLuint m_vbElv = 0;
  GLuint m_vbDisp = 0;

  // Index buffers at vertex stride 1, 2, 4, … over the same vertex data;
  // level L quarters the triangle count of L-1 (~33 % extra memory in total).
  static const int k_maxLod = lod::k_maxLevels;
  GLuint m_ebos[k_maxLod] = {};
  GLsizei m_idxCnts[k_maxLod] = {};
  int m_numLods = 0;
  float m_cellWorld = 0.0f; // world-unit size of one grid cell
  // World XZ coords of the grid's first/last vertex and the elevation range,
  // for frustum culling of off-screen grid rows/columns in draw().
  float m_x0 = 0.0f, m_x1 = 0.0f, m_z0 = 0.0f, m_z1 = 0.0f;
  float m_minElev = 0.0f, m_maxElev = 0.0f;

  std::vector<float> m_xz;
  float m_scaleXZ = 1.0f;
  bool m_hasDispl = false;
  float m_dispPeak = 0.0f;
  float m_dispScaleY = 0.0f;

  GLuint m_seaVao = 0;
  GLuint m_seaVbo = 0;

  // Subduction-zone overlay: an RGBA texture on a flat sea-level quad.
  Shader m_overlayShader;
  GLuint m_overlayVao = 0;
  GLuint m_overlayVbo = 0;
  GLuint m_overlayTex = 0;
  bool m_hasOverlay = false;

  float m_scaleY = 1.0f;

  Shader m_waterShader;
  GLuint m_waterVao = 0;
  GLuint m_waterVbXZ = 0;
  GLuint m_waterVbBath = 0;
  GLuint m_waterVbH = 0;
  GLuint m_waterEbos[k_maxLod] = {};
  GLsizei m_waterIdxCnts[k_maxLod] = {};
  int m_waterNumLods = 0;
  float m_waterCellWorld = 0.0f;
  t_idx m_simNx = 0;
  t_idx m_simNy = 0;
  bool m_simulating = false;
  float m_waterAnom = 1.0f;
  std::vector<float> m_waterH;
  std::vector<float> m_simBath;
};

} // namespace visualization
} // namespace tsunami_lab

#endif
