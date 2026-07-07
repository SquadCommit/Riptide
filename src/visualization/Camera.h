#ifndef TSUNAMI_LAB_VISUALIZATION_CAMERA_H
#define TSUNAMI_LAB_VISUALIZATION_CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace tsunami_lab {
namespace visualization {

class Camera {
public:
  glm::vec3 position() const {
    return m_target +
           glm::vec3(m_distance * std::cos(m_elevation) * std::sin(m_azimuth),
                     m_distance * std::sin(m_elevation),
                     m_distance * std::cos(m_elevation) * std::cos(m_azimuth));
  }

  glm::mat4 view() const {
    return glm::lookAt(position(), m_target, glm::vec3(0, 1, 0));
  }

  glm::mat4 projection(float i_aspect) const {
    return glm::perspective(glm::radians(45.0f), i_aspect, 0.1f, 10000.0f);
  }

  void onMouseDrag(float i_dx, float i_dy) {
    m_azimuth -= i_dx * 0.005f;
    m_elevation += i_dy * 0.005f;
    // clamp elevation so we don't flip over the poles
    if (m_elevation > 1.5f)
      m_elevation = 1.5f;
    if (m_elevation < -1.5f)
      m_elevation = -1.5f;
  }

  void onScroll(float i_dy) {
    m_distance *= (1.0f - i_dy * 0.1f);
    if (m_distance < 0.5f)
      m_distance = 0.5f;
    if (m_distance > 5000.0f)
      m_distance = 5000.0f;
  }

  void onMiddleDrag(float i_dx, float i_dy) {
    glm::vec3 l_right = glm::vec3(std::cos(m_azimuth), 0, -std::sin(m_azimuth));
    glm::vec3 l_up = glm::vec3(0, 1, 0);
    float l_speed = m_distance * 0.001f;
    m_target -= l_right * (i_dx * l_speed);
    m_target += l_up * (i_dy * l_speed);
  }

  void setTarget(glm::vec3 i_target) { m_target = i_target; }
  void setDistance(float i_d) { m_distance = i_d; }
  void setAzimuth(float i_a) { m_azimuth = i_a; }
  void setElevation(float i_e) {
    m_elevation = i_e;
    if (m_elevation > 1.5f)
      m_elevation = 1.5f;
    if (m_elevation < -1.5f)
      m_elevation = -1.5f;
  }
  float getDistance() const { return m_distance; }

  // Flat-map pan: moves target in the world XZ plane (lon/lat space).
  // Use instead of onMiddleDrag when the camera is in top-down globe mode.
  void onMapPan(float i_dx, float i_dy) {
    glm::vec3 l_right = glm::vec3(std::cos(m_azimuth), 0, -std::sin(m_azimuth));
    glm::vec3 l_fwd = glm::vec3(std::sin(m_azimuth), 0, std::cos(m_azimuth));
    float l_speed = m_distance * 0.001f;
    // Drag right → map moves right → target lon decreases → target.x -= dx
    m_target -= l_right * (i_dx * l_speed);
    // Drag down  → map moves down  → target lat increases → target.z decreases
    // (world Z = -lat, so decreasing Z = increasing lat = looking north)
    m_target -= l_fwd * (i_dy * l_speed);
    m_target.y = 0.0f;
  }

private:
  glm::vec3 m_target{0.0f, 0.0f, 0.0f};
  float m_distance{100.0f};
  float m_azimuth{0.0f};
  float m_elevation{0.5f};
};

} // namespace visualization
} // namespace tsunami_lab

#endif
