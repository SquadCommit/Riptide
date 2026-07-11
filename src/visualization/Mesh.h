#ifndef TSUNAMI_LAB_VISUALIZATION_MESH_H
#define TSUNAMI_LAB_VISUALIZATION_MESH_H

#include "../constants.h"
#include "Shader.h"
#include <GLES3/gl3.h>
#include <cstddef>
#include <vector>

namespace tsunami_lab {
namespace visualization {

class Mesh {
public:
  // i_nx/i_ny: number of cells; i_dx/i_dy: cell size in metres
  void init(t_idx i_nx, t_idx i_ny, float i_dx, float i_dy) {
    m_nx = i_nx;
    m_ny = i_ny;

    // XZ positions — uploaded once, never change
    std::vector<float> l_pos;
    l_pos.reserve(i_nx * i_ny * 2);
    for (t_idx iy = 0; iy < i_ny; iy++)
      for (t_idx ix = 0; ix < i_nx; ix++) {
        l_pos.push_back(ix * i_dx);
        l_pos.push_back(iy * i_dy);
      }

    // Y heights — updated every frame via uploadHeights()
    m_heights.assign(i_nx * i_ny, 0.0f);

    // index buffer: two triangles per quad
    std::vector<unsigned int> l_idx;
    l_idx.reserve((i_nx - 1) * (i_ny - 1) * 6);
    for (t_idx iy = 0; iy < i_ny - 1; iy++)
      for (t_idx ix = 0; ix < i_nx - 1; ix++) {
        unsigned int l_bl = (unsigned int)(iy * i_nx + ix);
        unsigned int l_br = l_bl + 1;
        unsigned int l_tl = l_bl + (unsigned int)i_nx;
        unsigned int l_tr = l_tl + 1;
        l_idx.push_back(l_bl);
        l_idx.push_back(l_br);
        l_idx.push_back(l_tl);
        l_idx.push_back(l_br);
        l_idx.push_back(l_tr);
        l_idx.push_back(l_tl);
      }
    m_indexCount = (GLsizei)l_idx.size();

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vboPos);
    glGenBuffers(1, &m_vboH);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    // attrib 0: XZ position (2 floats, static)
    glBindBuffer(GL_ARRAY_BUFFER, m_vboPos);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(l_pos.size() * sizeof(float)),
                 l_pos.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    // attrib 1: Y height (1 float, dynamic)
    glBindBuffer(GL_ARRAY_BUFFER, m_vboH);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_heights.size() * sizeof(float)),
                 m_heights.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(l_idx.size() * sizeof(unsigned int)),
                 l_idx.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
  }

  // stride = solver stride (m_nx + 2 ghost columns); copies interior cells only
  void uploadHeights(const t_real* i_data, t_idx i_stride) {
    for (t_idx iy = 0; iy < m_ny; iy++)
      for (t_idx ix = 0; ix < m_nx; ix++)
        m_heights[iy * m_nx + ix] =
            (float)i_data[(iy + 1) * i_stride + (ix + 1)];

    glBindBuffer(GL_ARRAY_BUFFER, m_vboH);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(m_heights.size() * sizeof(float)),
                    m_heights.data());
  }

  void draw(const Shader& i_shader) const {
    i_shader.use();
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
  }

  ~Mesh() {
    if (m_vao) {
      glDeleteVertexArrays(1, &m_vao);
    }
    if (m_vboPos) {
      glDeleteBuffers(1, &m_vboPos);
    }
    if (m_vboH) {
      glDeleteBuffers(1, &m_vboH);
    }
    if (m_ebo) {
      glDeleteBuffers(1, &m_ebo);
    }
  }

private:
  t_idx m_nx = 0, m_ny = 0;
  std::vector<float> m_heights;
  GLuint m_vao = 0, m_vboPos = 0, m_vboH = 0, m_ebo = 0;
  GLsizei m_indexCount = 0;
};

} // namespace visualization
} // namespace tsunami_lab

#endif
