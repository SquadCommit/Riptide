#ifndef TSUNAMI_LAB_VISUALIZATION_SCENARIO_H
#define TSUNAMI_LAB_VISUALIZATION_SCENARIO_H

#include "BBox.h"

namespace tsunami_lab {
namespace visualization {

/**
 * A historical earthquake used as a one-click starting point: the region to
 * load, the epicentre to place the fault at, and the moment magnitude to
 * derive the fault geometry from. Depth/strike/dip still come from Slab2 at
 * the epicentre (or the fallback), exactly as for a manual click.
 **/
struct Scenario {
  const char* name;
  BBox region;
  double epiLon;
  double epiLat;
  float magnitude;
};

constexpr Scenario k_scenarios[] = {
    {"Tohoku 2011 (Japan, Mw 9.1)",
     {138.0f, 148.0f, 34.0f, 42.0f},
     142.36,
     38.32,
     9.1f},
    {"Sumatra-Andaman 2004 (Mw 9.1)",
     {90.0f, 100.0f, -2.0f, 10.0f},
     95.85,
     3.30,
     9.1f},
    {"Chile / Valdivia 1960 (Mw 9.5)",
     {-78.0f, -68.0f, -45.0f, -34.0f},
     -73.05,
     -39.5,
     9.5f},
    {"Chile / Maule 2010 (Mw 8.8)",
     {-76.0f, -70.0f, -38.0f, -32.0f},
     -72.898,
     -35.909,
     8.8f},
    {"Alaska 1964 (Mw 9.2)",
     {-152.0f, -140.0f, 58.0f, 62.0f},
     -147.34,
     61.02,
     9.2f},
};
constexpr int k_numScenarios =
    (int)(sizeof(k_scenarios) / sizeof(k_scenarios[0]));

} // namespace visualization
} // namespace tsunami_lab

#endif
