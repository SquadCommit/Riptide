/**
 * @author Yannik Koellmann
 **/
#include "OmpDefaults.h"

#include <cstdlib>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace tsunami_lab {
namespace util {

void applySaneOmpScheduleDefault() {
#ifdef _OPENMP
  if (!std::getenv("OMP_SCHEDULE")) {
#ifdef __APPLE__
    // Apple Silicon: hybrid P/E cores share a uniform memory. With a static
    // schedule the slowest (efficiency) core gates every barrier; guided
    // rebalances and lets the E-cores contribute (~15% faster at 10 threads
    // on an M4, measured on DamBreak2d n=1000).
    omp_set_schedule(omp_sched_guided, 0);
#else
    // Homogeneous NUMA nodes (cluster): static keeps each thread on the
    // pages it first-touched; dynamic/guided would migrate rows across
    // NUMA domains (see chapter 9 of the docs).
    omp_set_schedule(omp_sched_static, 0);
#endif
  }
#endif
}

} // namespace util
} // namespace tsunami_lab
