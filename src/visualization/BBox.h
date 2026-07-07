#ifndef TSUNAMI_LAB_VISUALIZATION_BBOX_H
#define TSUNAMI_LAB_VISUALIZATION_BBOX_H

namespace tsunami_lab {
namespace visualization {

struct BBox {
  float lonMin = 0, lonMax = 0;
  float latMin = 0, latMax = 0;

  constexpr BBox() = default;
  constexpr BBox(float i_lonMin, float i_lonMax, float i_latMin, float i_latMax)
      : lonMin(i_lonMin), lonMax(i_lonMax), latMin(i_latMin), latMax(i_latMax) {
  }

  bool valid() const { return lonMax > lonMin && latMax > latMin; }

  float lonCenter() const { return (lonMin + lonMax) * 0.5f; }
  float latCenter() const { return (latMin + latMax) * 0.5f; }
  float lonSpan() const { return lonMax - lonMin; }
  float latSpan() const { return latMax - latMin; }
};

} // namespace visualization
} // namespace tsunami_lab

#endif
