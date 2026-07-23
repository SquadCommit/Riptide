#ifndef TSUNAMI_LAB_VISUALIZATION_SHADER_H
#define TSUNAMI_LAB_VISUALIZATION_SHADER_H

#include <GLES3/gl3.h>
#include <cstdio>
#include <fstream>
#include <glm/glm.hpp>
#include <sstream>
#include <string>

namespace tsunami_lab {
namespace visualization {

class Shader {
public:
  void buildFromFiles(const char* i_vertPath, const char* i_fragPath) {
    auto readFile = [](const char* i_path) -> std::string {
      std::ifstream f(i_path);
      if (!f.is_open()) {
        std::fprintf(stderr, "Shader: cannot open %s\n", i_path);
        return {};
      }
      std::ostringstream ss;
      ss << f.rdbuf();
      return ss.str();
    };
    std::string l_vert = readFile(i_vertPath);
    std::string l_frag = readFile(i_fragPath);
    if (!l_vert.empty() && !l_frag.empty())
      build(l_vert.c_str(), l_frag.c_str());
  }

  void build(const char* i_vert, const char* i_frag) {
    // The shader sources are written as GLSL 330 core (they predate the web
    // port); WebGL2 only accepts GLSL ES 300, which is feature-equivalent for
    // everything used here but requires explicit default precision.
    const std::string l_vertEs = toGlslEs300(i_vert);
    const std::string l_fragEs = toGlslEs300(i_frag);
    GLuint l_vs = compile(GL_VERTEX_SHADER, l_vertEs.c_str());
    GLuint l_fs = compile(GL_FRAGMENT_SHADER, l_fragEs.c_str());

    m_id = glCreateProgram();
    glAttachShader(m_id, l_vs);
    glAttachShader(m_id, l_fs);
    glLinkProgram(m_id);

    int l_ok;
    glGetProgramiv(m_id, GL_LINK_STATUS, &l_ok);
    if (!l_ok) {
      char l_log[512];
      glGetProgramInfoLog(m_id, 512, nullptr, l_log);
      std::fprintf(stderr, "Shader link error:\n%s\n", l_log);
    }

    glDeleteShader(l_vs);
    glDeleteShader(l_fs);
  }

  void use() const { glUseProgram(m_id); }

  void setMat4(const char* i_name, const glm::mat4& i_val) const {
    glUniformMatrix4fv(glGetUniformLocation(m_id, i_name), 1, GL_FALSE,
                       &i_val[0][0]);
  }

  void setFloat(const char* i_name, float i_val) const {
    glUniform1f(glGetUniformLocation(m_id, i_name), i_val);
  }

  void setInt(const char* i_name, int i_val) const {
    glUniform1i(glGetUniformLocation(m_id, i_name), i_val);
  }

  void setVec4(const char* i_name, const glm::vec4& i_val) const {
    glUniform4fv(glGetUniformLocation(m_id, i_name), 1, &i_val[0]);
  }

  ~Shader() {
    if (m_id)
      glDeleteProgram(m_id);
  }

private:
  GLuint m_id = 0;

  static std::string toGlslEs300(const char* i_src) {
    std::string l_s(i_src);
    static const std::string k_desktop = "#version 330 core";
    const size_t l_p = l_s.find(k_desktop);
    if (l_p != std::string::npos)
      l_s.replace(l_p, k_desktop.size(),
                  "#version 300 es\n"
                  "precision highp float;\n"
                  "precision highp int;\n"
                  "precision highp sampler2D;");
    return l_s;
  }

  GLuint compile(GLenum i_type, const char* i_src) {
    GLuint l_s = glCreateShader(i_type);
    glShaderSource(l_s, 1, &i_src, nullptr);
    glCompileShader(l_s);

    int l_ok;
    glGetShaderiv(l_s, GL_COMPILE_STATUS, &l_ok);
    if (!l_ok) {
      char l_log[512];
      glGetShaderInfoLog(l_s, 512, nullptr, l_log);
      std::fprintf(stderr, "Shader compile error:\n%s\n", l_log);
    }
    return l_s;
  }
};

} // namespace visualization
} // namespace tsunami_lab

#endif
