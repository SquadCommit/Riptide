12. Individual Phase Week 3 — GUI-Thread CPU Pinning
======================================================

Overview
--------

Goal for this week: run ``tsunami_lab_viz`` so the visualization stays on
one fixed CPU core while OpenMP gets a dedicated, undisturbed set of worker
threads — one per performance core — for maximum solver throughput.

Implementation
---------------

Thread architecture of ``tsunami_lab_viz``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tsunami_lab_viz`` runs three kinds of threads in one process: the GUI/render
loop (main thread), a background ``SolverThread`` that drives
``WavePropagation2d::timeStep`` in a loop, and — for the duration of each
``timeStep`` call — the OpenMP worker pool spawned by its parallel regions
(see :doc:`09_parallelization`).

Only the GUI thread never enters an OpenMP parallel region. The
``SolverThread`` *does*: the first time it hits a parallel construct, libgomp
treats it as the team's master thread and, if ``OMP_PROC_BIND`` is set,
pins it to the first entry of ``OMP_PLACES`` — so it is automatically covered
by the OpenMP placement below. The GUI thread is the only one left
unpinned, and since a busy GUI thread on the same core as an OpenMP worker
causes visible stutter and steals cycles from the solver, it is pinned
explicitly via ``pthread_setaffinity_np`` in
``tsunami_lab::util::pinThreadToCore()`` (``src/util/CpuAffinity.cpp``),
called at the very start of ``main()`` in ``src/main_viz.cpp``.

This pinning is opt-in through the ``TSUNAMI_VIZ_CORE`` environment
variable (a logical CPU id) so it has no effect unless requested, and is a
no-op on non-Linux platforms:

.. code-block:: bash

   TSUNAMI_VIZ_CORE=0 ./build/tsunami_lab_viz

Running on different machines: ``scripts/viz_pinning_env.sh``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Which logical CPU id to reserve, and which ids to hand to OpenMP, depends on
the machine's core topology (see below) — the concrete numbers used above
(``TSUNAMI_VIZ_CORE=0``, cores 1–11 for OpenMP) only apply to the 12-core
machine they were measured on. Rather than re-deriving them by hand on every
new machine, ``scripts/viz_pinning_env.sh`` detects the topology and computes
them automatically:

.. code-block:: bash

   # option 1: detect + run in one step
   scripts/viz_pinning_env.sh ./build/tsunami_lab_viz

   # option 2: export into the current shell, then run manually
   eval "$(scripts/viz_pinning_env.sh)"
   ./build/tsunami_lab_viz

On **Linux** it distinguishes the same two cases described below: on a
hybrid CPU it reads ``/sys/devices/cpu_core/cpus`` / ``cpu_atom/cpus`` and
reserves an E-core for the GUI while giving OpenMP all P-cores; otherwise it
groups logical CPUs by physical core via ``lscpu -p=CPU,CORE,ONLINE``,
reserves one physical core for the GUI and gives OpenMP one thread per
remaining physical core (leaving SMT siblings idle). If ``lscpu`` isn't
installed, or only one physical core is found, it leaves the pinning
variables unset rather than guessing.

On **macOS** there is no user-space hard CPU-affinity API — no
``pthread_setaffinity_np`` equivalent, and libomp doesn't honour
``OMP_PLACES`` there — so ``pinThreadToCore()`` is a no-op by design (see
above) and the script does not set ``TSUNAMI_VIZ_CORE``/``OMP_PLACES``/
``OMP_PROC_BIND`` at all. It still tunes ``OMP_NUM_THREADS`` via
``sysctl``: on Apple Silicon it uses ``hw.perflevel0.physicalcpu`` (the
performance-core count, ``hw.perflevel1.physicalcpu`` efficiency cores
excluded); on Intel Macs / unknown topologies it uses
``hw.physicalcpu - 1``, reserving one core for the GUI *by count only* — the
OS scheduler, not the app, decides which physical core actually runs it.

It always prints what it detected to stderr, so the chosen cores/counts
stay visible even when redirected into ``eval``.

Finding which cores to use (manual / how the script decides)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The goal is to reserve **one fixed core for the GUI thread** and let
**OpenMP use as many threads as there are performance cores**, without the
two competing for the same physical core. Which cores count as "performance"
depends on the CPU:

* **Hybrid CPUs with P-cores/E-cores** (Intel Alder Lake and newer): the
  kernel exposes the split directly:

  .. code-block:: bash

     cat /sys/devices/cpu_core/cpus   # performance (P) cores
     cat /sys/devices/cpu_atom/cpus   # efficiency (E) cores

  Pin the GUI thread to one E-core (it doesn't need P-core performance) and
  give OpenMP the P-core list.

* **Homogeneous CPUs with SMT/Hyper-Threading** (e.g. AMD Ryzen, most Intel
  desktop/server chips without E-cores): there are no "performance cores" in
  the hybrid sense — the relevant distinction is *physical core* vs.
  *SMT sibling*. For a compute-bound kernel like the wave solver, running one
  OpenMP thread per **physical** core beats using both SMT siblings (the
  siblings share execution units, so the second one adds little throughput
  but doubles synchronisation overhead). List the physical-core groups with:

  .. code-block:: bash

     lscpu -e=CPU,CORE,ONLINE

  Two logical CPUs with the same ``CORE`` value are SMT siblings of the same
  physical core; pick one per group for OpenMP and leave the rest — including
  one full physical core reserved for the GUI thread — out of the OpenMP set.
  Example on a 12-core/24-thread machine (CPUs 0–11 and 12–23 are SMT
  sibling pairs, i.e. CPU *n* and *n+12* share a physical core): reserve
  physical core 0 (logical CPUs 0 and 12) for the GUI and give OpenMP one
  thread per remaining physical core (1–11, i.e. logical CPUs 1–11):

  .. code-block:: bash

     TSUNAMI_VIZ_CORE=0 \
     OMP_NUM_THREADS=11 OMP_PROC_BIND=close \
     OMP_PLACES="{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11}" \
     ./build/tsunami_lab_viz

* **Multi-socket / NUMA machines** (see the Grace benchmarks in
  :doc:`09_parallelization`): also keep the OpenMP set within a single NUMA
  node (``numactl -H`` or ``lscpu`` show the node boundaries) — crossing
  sockets is far more costly than losing one core to the GUI.

Note that ``OMP_PLACES`` only affects OpenMP threads (including the
``SolverThread`` once it enters a parallel region); it does not touch the
GUI thread, which is why ``TSUNAMI_VIZ_CORE`` is needed in addition to the
existing ``OMP_*`` variables. Wrapping the whole process in ``taskset``
instead is *not* equivalent — it would also confine the OpenMP threads to
the same restricted set and remove the separation.

Solver Vectorization & Dry-Cell CFL Fix
----------------------------------------

Follow-up performance work on ``WavePropagation2d`` / ``FWave`` (measured on
an Apple M4, ``DamBreak2d`` with 1000x1000 cells):

- ``FWave::netUpdates`` rewritten branch-free (wet/dry handling and wave
  accumulation via selects, same semantics incl. NaN cases) and force-inlined.
- X-sweep decoupled through per-thread edge buffers: net updates are stored
  per edge, then applied in a second elementwise loop. This removes the
  read-write overlap on cell ``ix+1`` between consecutive edges — together
  with ``__restrict`` pointers all sweep loops now auto-vectorize (NEON,
  verified with ``-Rpass=loop-vectorize``).
- Memory passes fused: the copy pass now lives inside the X-sweep, the
  NaN/negativity clamp inside the black Y-phase (5 grid passes → 3).
- ``OMP_SCHEDULE`` fallback is now platform-dependent: ``guided`` on Apple
  Silicon (with ``static`` the slowest E-core gates every barrier; guided
  lets E-cores contribute), ``static`` elsewhere (NUMA first-touch).

Result: 10.9 → 5.0 ns per cell and iteration single-threaded, 3.6 → 1.15 ns
with 10 threads (~3x).

Separately, the visualization's time-scale factor collapsed (x90 → x7) once
the wave reached a coast: drying cells (``0 < h <= c_dryTolerance``) keep
their leftover momentum frozen (the solver masks their updates), and
``SolverThread::maxWaveSpeed()`` counted them via ``u = hu/h`` with a tiny
``h``, shrinking dt for the rest of the run. Fixed twofold: ``maxWaveSpeed``
skips cells the solver treats as dry, and the clamp phase of ``timeStep``
flushes ``hu``/``hv`` in dry cells. Regression tests:
``[WaveProp2dDryFlush]``, ``[DryCFL]``.

Individual Contributions
-------------------------

- **Yannik Köllmann:** ``tsunami_lab::util::pinThreadToCore()`` GUI-thread
  pinning via ``TSUNAMI_VIZ_CORE``; branchless/vectorized FWave kernel, fused
  sweep passes and dry-cell CFL fix (see above); wrote this chapter.
- **Jan Vogt:**
- **Mika Brückner:**
