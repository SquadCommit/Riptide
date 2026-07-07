#ifndef TSUNAMI_LAB_VISUALIZATION_LOD_H
#define TSUNAMI_LAB_VISUALIZATION_LOD_H

#include <algorithm>
#include <cmath>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

namespace tsunami_lab {
namespace visualization {
namespace lod {

// Number of index-buffer levels built per mesh (vertex stride 1, 2, 4, …,
// 2^(k_maxLevels-1)). Coarser levels cost ~1/4 of the previous one, so the
// whole chain adds ~33 % to the stride-1 index memory.
const int k_maxLevels = 8;

// Builds a quad-grid index list over a w×h vertex grid, sampling every
// i_stride-th vertex, ordered row-major (all quads of a row are contiguous)
// so drawGridWindow() can render a rectangular sub-window as one contiguous
// index span per row. The last row/column is clamped into the final quads so
// every level spans the full mesh extent. Winding matches the stride-1 mesh.
inline void
buildIndices(int i_w, int i_h, int i_stride, std::vector<unsigned int>& o_idx) {
  o_idx.clear();
  for (int l_j = 0; l_j < i_h - 1; l_j += i_stride) {
    const int l_j2 = std::min(l_j + i_stride, i_h - 1);
    for (int l_i = 0; l_i < i_w - 1; l_i += i_stride) {
      const int l_i2 = std::min(l_i + i_stride, i_w - 1);
      const unsigned int l_v00 = (unsigned int)(l_j * i_w + l_i);
      const unsigned int l_v01 = (unsigned int)(l_j * i_w + l_i2);
      const unsigned int l_v10 = (unsigned int)(l_j2 * i_w + l_i);
      const unsigned int l_v11 = (unsigned int)(l_j2 * i_w + l_i2);
      o_idx.push_back(l_v00);
      o_idx.push_back(l_v10);
      o_idx.push_back(l_v01);
      o_idx.push_back(l_v10);
      o_idx.push_back(l_v11);
      o_idx.push_back(l_v01);
    }
  }
}

// Smallest level whose cells still project to >= ~1.5 px on screen. Rendering
// sub-pixel triangles adds no visible detail but multiplies the MSAA fill
// cost enough to stall the GPU when a dense mesh fills the view.
//
// i_cellWorld:   world-unit size of one stride-1 grid cell
// i_numLevels:   how many levels were actually built for this mesh
// i_camDistance: camera orbit distance, world units (45° vertical FOV assumed)
// i_viewportPx:  framebuffer height in pixels
inline int pickLevel(float i_cellWorld,
                     int i_numLevels,
                     float i_camDistance,
                     int i_viewportPx) {
  if (i_numLevels <= 1 || i_cellWorld <= 0.0f || i_viewportPx <= 0)
    return 0;
  // The viewport height spans 2 * distance * tan(fov/2) world units.
  const float l_px = i_cellWorld * (float)i_viewportPx /
                     (2.0f * std::max(1.0f, i_camDistance) * 0.41421356f);
  int l_level = 0;
  while (l_level + 1 < i_numLevels && l_px * (float)(1 << l_level) < 1.5f)
    l_level++;
  return l_level;
}

// World-space XZ bounding box of the view frustum's intersection with the
// horizontal slab y ∈ [i_minY, i_maxY] (the vertical extent of the mesh).
// The intersection is a convex polytope; its XZ extremes sit on vertices
// created where the frustum's edges cross the slab planes, so ALL 12 edges
// of the frustum (4 side edges plus the 4 near-plane and 4 far-plane
// rectangle edges) are clipped against the slab. Clipping only the 4 side
// edges — the viewport-corner rays — is NOT enough: at a grazing tilt those
// rays leave the thin slab within a short distance (the upper ones through
// its top, the lower ones through its bottom), while near-horizontal view
// directions through the frustum's interior stay inside the slab all the
// way to the far plane; their reach shows up only on the far-plane edges.
// Missing them culled distant, clearly-visible terrain. Used to cull
// off-screen grid rows/columns when zoomed in.
inline void frustumFootprintXZ(const glm::mat4& i_vp,
                               float i_minY,
                               float i_maxY,
                               float& o_minX,
                               float& o_maxX,
                               float& o_minZ,
                               float& o_maxZ) {
  const glm::mat4 l_inv = glm::inverse(i_vp);
  o_minX = o_minZ = 3.4e38f;
  o_maxX = o_maxZ = -3.4e38f;
  const float l_lo = std::min(i_minY, i_maxY) - 0.01f;
  const float l_hi = std::max(i_minY, i_maxY) + 0.01f;

  // The 8 frustum corners: near plane first (indices 0-3), then far.
  const float l_ndc[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
  glm::vec3 l_v[8];
  for (int l_p = 0; l_p < 2; l_p++)
    for (int l_c = 0; l_c < 4; l_c++) {
      const glm::vec4 l_h = l_inv * glm::vec4(l_ndc[l_c][0], l_ndc[l_c][1],
                                              l_p == 0 ? -1.0f : 1.0f, 1.0f);
      l_v[l_p * 4 + l_c] = glm::vec3(l_h) / l_h.w;
    }

  static const int k_edges[12][2] = {
      {0, 4}, {1, 5}, {2, 6}, {3, 7}, // side edges (corner rays)
      {0, 1}, {1, 3}, {3, 2}, {2, 0}, // near-plane rectangle
      {4, 5}, {5, 7}, {7, 6}, {6, 4}, // far-plane rectangle
  };
  for (int l_e = 0; l_e < 12; l_e++) {
    const glm::vec3& l_a = l_v[k_edges[l_e][0]];
    const glm::vec3 l_d = l_v[k_edges[l_e][1]] - l_a;
    float l_t0 = 0.0f, l_t1 = 1.0f;
    if (std::abs(l_d.y) > 1e-9f) {
      const float l_ta = (l_lo - l_a.y) / l_d.y;
      const float l_tb = (l_hi - l_a.y) / l_d.y;
      l_t0 = std::max(0.0f, std::min(l_ta, l_tb));
      l_t1 = std::min(1.0f, std::max(l_ta, l_tb));
      if (l_t1 < l_t0)
        continue; // edge misses the slab entirely
    } else if (l_a.y < l_lo || l_a.y > l_hi) {
      continue; // horizontal edge outside the slab
    }
    for (int l_k = 0; l_k < 2; l_k++) {
      const glm::vec3 l_p = l_a + (l_k == 0 ? l_t0 : l_t1) * l_d;
      o_minX = std::min(o_minX, l_p.x);
      o_maxX = std::max(o_maxX, l_p.x);
      o_minZ = std::min(o_minZ, l_p.z);
      o_maxZ = std::max(o_maxZ, l_p.z);
    }
  }

  // No edge touched the slab (degenerate matrix or slab fully outside the
  // frustum): return an unbounded box so callers fall back to the full grid
  // instead of culling geometry.
  if (o_minX > o_maxX || o_minZ > o_maxZ) {
    o_minX = o_minZ = -3.4e38f;
    o_maxX = o_maxZ = 3.4e38f;
  }
}

// Maps a world-coordinate window [i_lo, i_hi] to the covered vertex-index
// range of a grid axis whose vertices run linearly from i_a0 (vertex 0) to
// i_a1 (vertex i_n-1); i_a1 < i_a0 is fine (the region/globe Z axis is
// inverted). One extra vertex of margin on each side.
inline void visibleRange(float i_a0,
                         float i_a1,
                         int i_n,
                         float i_lo,
                         float i_hi,
                         int& o_0,
                         int& o_1) {
  if (i_n < 2 || i_a1 == i_a0) {
    o_0 = 0;
    o_1 = i_n - 1;
    return;
  }
  float l_f0 = (i_lo - i_a0) / (i_a1 - i_a0) * (float)(i_n - 1);
  float l_f1 = (i_hi - i_a0) / (i_a1 - i_a0) * (float)(i_n - 1);
  if (l_f0 > l_f1)
    std::swap(l_f0, l_f1);
  // Clamp before the int cast: an unbounded footprint is ±3.4e38.
  l_f0 = std::max(-2.0f, std::min((float)i_n + 1.0f, l_f0));
  l_f1 = std::max(-2.0f, std::min((float)i_n + 1.0f, l_f1));
  o_0 = std::max(0, (int)std::floor(l_f0) - 1);
  o_1 = std::min(i_n - 1, (int)std::ceil(l_f1) + 1);
  // At grazing camera angles the slab-intersection footprint can land
  // entirely outside the grid's extent on one side (both bounds clamp to
  // the same edge), inverting the range. That must never cull real
  // geometry, so fall back to the full grid — same "conservative superset"
  // rule frustumFootprintXZ itself follows.
  if (o_0 > o_1) {
    o_0 = 0;
    o_1 = i_n - 1;
  }
}

// Draws the quads of LOD level log2(i_stride) that cover the stride-1 vertex
// window [i_i0..i_i1] × [i_j0..i_j1], as one glDrawElements per quad row (the
// row-major index layout makes each row's window a contiguous span). The
// matching LOD index buffer must be bound as GL_ELEMENT_ARRAY_BUFFER. This is
// what makes close zooms cheap: only on-screen rows/columns are submitted
// instead of the whole mesh.
inline void drawGridWindow(
    int i_w, int i_h, int i_stride, int i_i0, int i_i1, int i_j0, int i_j1) {
  if (i_i1 < i_i0 || i_j1 < i_j0 || i_w < 2 || i_h < 2)
    return;
  const int l_cols = (i_w - 2) / i_stride + 1; // quads per row at this level
  const int l_rows = (i_h - 2) / i_stride + 1;
  const int l_c0 = std::min(i_i0 / i_stride, l_cols - 1);
  const int l_c1 = std::min(i_i1 / i_stride, l_cols - 1);
  const int l_r0 = std::min(i_j0 / i_stride, l_rows - 1);
  const int l_r1 = std::min(i_j1 / i_stride, l_rows - 1);
  const GLsizei l_cnt = (GLsizei)((l_c1 - l_c0 + 1) * 6);
  for (int l_r = l_r0; l_r <= l_r1; l_r++)
    glDrawElements(GL_TRIANGLES, l_cnt, GL_UNSIGNED_INT,
                   (const void*)((size_t)((size_t)l_r * l_cols + l_c0) * 6 *
                                 sizeof(unsigned int)));
}

// Splits [i_lo, i_hi] into up to i_n contiguous, non-overlapping bands.
// Returns the actual band count (<= i_n, 0 if the range is empty).
inline int splitBands(int i_lo, int i_hi, int i_n, int* o_starts, int* o_ends) {
  if (i_hi < i_lo)
    return 0;
  const int l_total = i_hi - i_lo + 1;
  const int l_n = std::min(i_n, std::max(1, l_total));
  int l_pos = i_lo;
  for (int l_b = 0; l_b < l_n; l_b++) {
    const int l_end = i_lo + (int)((long long)(l_b + 1) * l_total / l_n) - 1;
    o_starts[l_b] = l_pos;
    o_ends[l_b] = std::max(l_pos, l_end);
    l_pos = o_ends[l_b] + 1;
  }
  return l_n;
}

// Draws a grid window at a LOD that varies per screen-space band instead of
// one level for the whole window. A single global level (picked from camera
// orbit distance alone) is fine for a top-down view, where every visible
// cell is roughly equidistant, but badly wrong at a grazing/tilted-down
// angle: the near part of the same window can be tens of world units away
// while the far part is thousands away, so one stride is either far too
// coarse for the near part or far too fine for the far part. Splitting the
// window into up to i_maxBands x i_maxBands tiles and picking each tile's
// level from its own midpoint's true camera distance fixes both.
template <class BindLevelFn>
inline void drawGridWindowBanded(int i_w,
                                 int i_h,
                                 int i_i0,
                                 int i_i1,
                                 int i_j0,
                                 int i_j1,
                                 float i_x0,
                                 float i_x1,
                                 float i_z0,
                                 float i_z1,
                                 const glm::vec3& i_camPos,
                                 float i_cellWorld,
                                 int i_numLevels,
                                 int i_viewportPx,
                                 int i_maxBands,
                                 BindLevelFn i_bindLevel) {
  int l_iS[8], l_iE[8], l_jS[8], l_jE[8];
  i_maxBands = std::min(i_maxBands, 8);
  const int l_ni = splitBands(i_i0, i_i1, i_maxBands, l_iS, l_iE);
  const int l_nj = splitBands(i_j0, i_j1, i_maxBands, l_jS, l_jE);
  for (int l_bj = 0; l_bj < l_nj; l_bj++) {
    const float l_wz = i_z0 + (i_z1 - i_z0) * 0.5f *
                                  (float)(l_jS[l_bj] + l_jE[l_bj]) /
                                  (float)std::max(1, i_h - 1);
    for (int l_bi = 0; l_bi < l_ni; l_bi++) {
      const float l_wx = i_x0 + (i_x1 - i_x0) * 0.5f *
                                    (float)(l_iS[l_bi] + l_iE[l_bi]) /
                                    (float)std::max(1, i_w - 1);
      const float l_dist = glm::length(i_camPos - glm::vec3(l_wx, 0.0f, l_wz));
      const int l_level =
          pickLevel(i_cellWorld, i_numLevels, l_dist, i_viewportPx);
      i_bindLevel(l_level);
      drawGridWindow(i_w, i_h, 1 << l_level, l_iS[l_bi], l_iE[l_bi], l_jS[l_bj],
                     l_jE[l_bj]);
    }
  }
}

} // namespace lod
} // namespace visualization
} // namespace tsunami_lab

#endif
