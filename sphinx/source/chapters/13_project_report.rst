13. Project Report — Interactive Real-Time Tsunami Simulation
==============================================================

.. contents:: On this page
   :local:
   :depth: 2

Abstract
--------

The individual phase of the Tsunami Lab turned a batch finite-volume solver
into an **interactive, physically motivated tsunami laboratory**.  Over five
weeks (17 June – 27 July 2026, 134 commits) the project passed through three
stages:

1. a **real-time 3D visualization** written directly against OpenGL — no game
   engine, no scene graph, no rendering framework;
2. a **physically accurate earthquake source**, replacing the initial Gaussian
   bump with a full Okada rectangular-dislocation model driven by published
   scaling laws, real subduction-zone geometry and the Tanioka–Satake
   sloping-seafloor correction;
3. a **port of the entire application to the browser** — the same C++ solver
   and the same renderer, compiled to WebAssembly and WebGL2 behind a React
   frontend, publicly deployed.

The result is a tool where a user picks a subduction zone on a globe, sets a
moment magnitude, clicks a location, and watches a tsunami — generated from a
fault whose depth, strike and dip come from the USGS Slab2 model and whose
length, width and slip come from interface-earthquake scaling relations —
propagate live over GEBCO bathymetry.

**Live application:** https://ykoellmann.github.io/tsunami_lab/

Starting point and goal
-----------------------

At the end of the course phase the project was a competent but entirely
**offline** pipeline: ``WavePropagation2d`` stepped a grid, ``main.cpp``
drove a time loop, and every output went through a NetCDF/CSV writer to be
inspected afterwards in ParaView.  Between running a simulation and seeing a
result there was a long, non-interactive wait, and at the end of it stood a
fixed image rather than an experiment.

The goal of the individual phase was to close that loop:

* **Real time.** The solver runs continuously in the background; the wave is
  visible as it develops, at interactive frame rates, without any file I/O.
* **Interactive.** The user chooses the region and triggers the earthquake by
  clicking on the seafloor, with no configuration files and no intermediate
  steps.
* **Physically defensible.** The initial condition is not a hand-tuned bump.
  A magnitude and a location are enough, and everything between them and the
  water surface is derived from published seismological models.

The third point is what the project spent the most research effort on, and it
is treated in its own section below.

.. figure:: ../_images/individual_phase/globe_view.png
   :alt: Globe view with a selection rectangle over Southeast Asia
   :width: 85%

   Entry point: a global bathymetry view with the Slab2 subduction zones
   overlaid.  A drag selects the simulation domain.

Part I — The renderer: OpenGL written from scratch
---------------------------------------------------

No engine, no rendering framework
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The visualization is **not** built on a 3D engine.  There is no Unity, no
Unreal, no OpenSceneGraph, no VTK, no three.js, no Babylon.js — and no
higher-level rendering abstraction of any kind between the application and
the graphics driver.  Every triangle the application draws is the result of
buffers we allocate, shaders we author and draw calls we issue.

The native build linked exactly four third-party pieces, none of which draws
anything:

=====================  ======================================================
Library                What it does — and only that
=====================  ======================================================
**GLFW**               Opens an OS window and delivers mouse/keyboard events
**GLAD**               Resolves OpenGL function pointers at runtime
**Dear ImGui**         Draws the 2D sidebar widgets of the desktop UI
**glm**                Vector/matrix maths (a header-only ``<cmath>``)
=====================  ======================================================

Everything that makes the picture a picture was written for this project:

**Mesh and buffer management** (``src/visualization/Mesh.h``).  The terrain and
water surfaces are regular ``nx × ny`` grids uploaded as a VAO with a static
XZ-position VBO, a dynamic height VBO and an index buffer of two triangles per
quad.  Only the height attribute is re-uploaded per frame via
``glBufferSubData``; positions and indices are uploaded once at load time.  The
upload also strips the solver's ghost layer, so the renderer never sees a
boundary cell.

**Shader pipeline** (``src/visualization/Shader.h``).  Source loading,
compilation, program linking, error-log reporting and uniform plumbing are all
hand-rolled — about 120 lines that grew a second job during the web port (see
below).  The GLSL itself is 11 hand-written files (256 lines) under
``src/visualization/shaders/``: separate vertex/fragment pairs for the globe
terrain, globe selection rectangle, region terrain, sea-level plane and water
surface.

**Camera and picking** (``src/visualization/Camera.h``).  An orbit camera
parameterised by target, distance, azimuth and elevation, producing its own
``lookAt`` view matrix and perspective projection.  Mouse drag, scroll zoom,
middle-drag pan and a separate flat-map pan mode for the globe are all
implemented directly on those parameters.  Clicking the seafloor to place an
earthquake requires unprojecting the cursor into a world-space ray and
intersecting it with the terrain — the picking maths is ours as well, as is
the round trip between world coordinates and geographic lon/lat.

**Level of detail and culling** (``src/visualization/Lod.h``, 273 lines).  A
full-resolution GEBCO region is far too dense to draw naively at every zoom
level.  The renderer builds an **8-level chain of index buffers** over the same
vertex grid (vertex stride 1, 2, 4, … 128), costing only ~33 % extra index
memory because each level is a quarter of the previous one.  Per frame it

* picks the coarsest level whose cells still project to roughly 1.5 pixels —
  sub-pixel triangles add no visible detail but multiply the MSAA fill cost
  enough to stall the GPU;
* intersects the view frustum with the terrain's XZ plane and draws only the
  rectangular sub-window of the grid that survives, as one contiguous index
  span per row.

The index layout is row-major precisely so that this windowed draw is possible
without rebuilding buffers.  The last row and column are clamped into the final
quads at every level, so no level leaves a gap at the mesh edge.

**Shading and colour** (the fragment shaders).  Hill shading is derived
per-fragment from screen-space derivatives (``dFdx``/``dFdy``) of the world
position, so no normal attribute needs to be stored or updated.  The
hypsometric terrain colormap (deep-ocean blue → shelf → coastal green →
highland brown) and the diverging wave colormap (a warm jet ramp for crests, a
cool violet ramp for troughs) are piecewise ``mix`` chains written into the
shaders directly.  Vertical exaggeration is applied in the vertex shader and is
depth-aware: it tapers toward 1× in shallow water, because shoaling already
amplifies the wave there and full exaggeration produced extreme spikes at the
coast.

**Rendering order and correctness details.**  The translucent sea-level plane,
the water sheet and the Slab2 subduction overlay are separate passes with
their own blending and depth settings.  Getting them to coexist took a series
of small, unglamorous fixes that are visible in the git history: terrain being
culled at grazing camera angles, coastal seabed artefacts showing through the
water sheet, speckling at far zoom (solved by raising MSAA to 8×), and a pale
strip of bare seabed wherever the coarse simulation shoreline disagreed with
the fine terrain coast — fixed by snapping dry vertices to sea level in the
vertex shader instead of discarding them, so the water forms a gap-free sheet
that land occludes via the depth test.

In total the renderer is **~2,800 lines of C++ plus 256 lines of GLSL**, all of
it project code.

.. figure:: ../_images/individual_phase/region_preview.png
   :alt: 3D bathymetry terrain preview of the selected region
   :width: 85%

   Region view: GEBCO bathymetry as a shaded 3D mesh with hypsometric
   colouring and a translucent sea-level plane.

Part II — Seafloor displacement: the physics deep dive
--------------------------------------------------------

This is where the project invested the most reading, the most derivation and
the most validation effort.  The question is deceptively simple — *the user
says "magnitude 9.1, here"; what does the water surface look like at
t = 0?* — and answering it honestly required assembling five separate pieces
of published seismology.

Why the Gaussian had to go
~~~~~~~~~~~~~~~~~~~~~~~~~~

The first implementation (``GaussianDisplacement``) was a radially symmetric
bell:

.. math::

   d(r) = A \exp\!\left(-\frac{r^2}{2\sigma^2}\right)

It is trivial to implement, it produces a plausible-looking wave, and it is
**physically meaningless**.  Its amplitude and width are free parameters with
no connection to any earthquake; it is radially symmetric, whereas real
megathrust ruptures are strongly elongated along strike; it produces only
uplift, whereas real co-seismic deformation pairs uplift over the shallow part
of the fault with subsidence behind it — and that uplift/subsidence dipole is
what sets the leading-depression-versus-leading-elevation character of the real
wave.  A tsunami started from a Gaussian is a nice animation of the *solver*
and tells you nothing about the *earthquake*.

Replacing it defined the second half of the individual phase.

The chain from magnitude to water surface
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The user supplies exactly two things: a **moment magnitude** :math:`M_w` and a
**click location** :math:`(\lambda, \varphi)`.  Everything else is derived:

.. code-block:: text

   user click + Mw
          │
          ├─► Slab2 (Hayes et al. 2018)      ──► depth, strike, dip
          │      └─ also acts as a plausibility filter (NaN = not a
          │         subduction interface ⇒ reject the source)
          │
          ├─► Strasser et al. (2010)          ──► length L, width W
          │      └─ slip D from moment consistency, not a regression
          │      └─ fallback outside Slab2: Wells & Coppersmith (1994)
          │
          └─► fixed assumptions               ──► rake 90°, ν = 0.25
                          │
                          ▼
              Okada (1985 / 1992)  ──►  u_z, and u_x / u_y
                          │
                          ▼
              Tanioka & Satake (1996) correction over sloping seafloor
                          │
                          ▼
              effective vertical displacement  ──►  initial sea surface

Okada (1985 / 1992) — the dislocation model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

`Okada (1985) <https://doi.org/10.1785/BSSA0750041135>`__ derives the static
surface displacement produced by a rectangular dislocation buried in a
homogeneous, isotropic elastic half-space; `Okada (1992)
<https://doi.org/10.1785/BSSA0820021018>`__ extends the closed-form
expressions to internal deformation and tidies up several singular cases.  The
half-space assumption trades geological realism for closed-form tractability:
no volumetric mesh and no numerical PDE solve are needed, so the field can be
evaluated per grid cell at negligible cost — which is exactly what an
interactive application requires.

A single fault plane is described by **nine parameters**: centroid position
:math:`(x, y)`, depth of the top edge :math:`d`, strike :math:`\phi`, dip
:math:`\delta`, rake :math:`\lambda`, along-strike length :math:`L`, down-dip
width :math:`W` and slip magnitude :math:`U`.  These map one-to-one onto the
``OkadaDisplacement`` constructor.

The implementation (``src/displacement/OkadaDisplacement.cpp``, 282 lines) is a
direct transcription of the closed-form solution and is genuinely fiddly:

* The query point is rotated into the fault-local frame where :math:`+x` runs
  along strike, using the **geographic** strike convention (clockwise from
  North), and rotated back afterwards.
* The rectangle is evaluated in **Chinnery's notation** — the field is the
  alternating sum :math:`f(\xi_2,\eta_2) - f(\xi_2,\eta_1) - f(\xi_1,\eta_2) +
  f(\xi_1,\eta_1)` over the four fault corners.
* The auxiliary terms :math:`I_1` … :math:`I_5` all contain a
  :math:`1/\cos\delta` factor that blows up for a vertical fault, so each one
  carries an explicit **:math:`\cos\delta \to 0` limit branch**, plus epsilon
  guards on every denominator and logarithm argument.

Only the vertical component drives the tsunami initial condition, but the
horizontal components :math:`u_x, u_y` (Okada 1985, eqs. 25/26) are computed as
well — they are needed for the Tanioka–Satake correction below.

Validation against ``okada85.m``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Okada equations are notoriously error-prone to transcribe: sign
conventions, angle definitions and singularity handling all invite mistakes
that produce fields which *look* right and are quantitatively wrong.  A
self-consistency test would not have caught that.

The implementation was therefore cross-checked against
`okada85.m <https://github.com/IPGP/deformation-lib/blob/master/okada/okada85.m>`__
from the IPGP ``deformation-lib`` package — the widely used MATLAB reference
implementation — run unmodified in **Octave 11.3.0**:

* **81 configurations** were compared: 9 fault parameter sets × 9 query points,
  spanning thrust, normal, oblique and pure strike-slip rakes, dips from 10° to
  the near-vertical 89° singularity, and both centroid and off-centre queries.
* **Maximum deviation: ~5·10⁻¹¹ absolute / ~8·10⁻⁹ relative** — floating-point
  noise, not a systematic error.
* Seven of those cases are pinned as regression tests in
  ``OkadaDisplacement.test.cpp`` with their reference values to ten significant
  digits, alongside symmetry tests, far-field decay tests, and a brute-force
  finiteness sweep over dips {1°, 45°, 89.9°, 90°} × rakes {0°, 45°, 90°, −90°}
  × depths, asserting that no input produces NaN or Inf.

Fault geometry: Wells & Coppersmith vs. Strasser et al.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Okada needs :math:`L`, :math:`W` and :math:`U`; the user supplies only
:math:`M_w`.  Closing that gap required a scaling relation, and the obvious
first choice turned out to be the wrong one.

**Wells & Coppersmith (1994)** — `doi:10.1785/BSSA0840040974
<https://doi.org/10.1785/BSSA0840040974>`__ — is the classic reference:

.. math::

   \begin{aligned}
   \log_{10}(RLD) &= -2.44 + 0.59\,M_w \\
   \log_{10}(RW)  &= -1.01 + 0.32\,M_w \\
   \log_{10}(AD)  &= -4.80 + 0.69\,M_w
   \end{aligned}

But reading the paper rather than just the equations revealed the problem: the
regressions are calibrated on **crustal** earthquakes (strike-slip, reverse,
normal) up to about :math:`M_w` 8.1, and the authors **explicitly exclude
subduction-interface events** from the dataset.  Extrapolated to the
:math:`M_w` 9+ megathrust events that actually generate ocean-crossing
tsunamis, they produce ruptures that are far too long and far too narrow —
aspect ratios beyond 10:1, which is not what a megathrust looks like.

**Strasser, Arango & Bommer (2010)** — `doi:10.1785/gssrl.81.6.941
<https://doi.org/10.1785/gssrl.81.6.941>`__ — provides regressions calibrated
specifically on subduction-interface earthquakes:

.. math::

   \log_{10}(L) = -2.477 + 0.585\,M_w, \qquad
   \log_{10}(W) = -0.882 + 0.351\,M_w \quad \text{(km)}

The slip is deliberately **not** taken from a third independent regression.
Three independent fits do not have to be mutually consistent, and an
inconsistent triple silently produces a fault carrying the wrong seismic
moment.  Instead the slip is chosen so that the fault carries *exactly* the
moment of the requested magnitude:

.. math::

   D = \frac{M_0}{\mu L W}, \qquad M_0 = 10^{1.5 M_w + 9.1}\ \text{N\,m},
   \qquad \mu = 40\ \text{GPa}

With this construction :math:`M_w` 9.1 yields a fault of roughly
**702 km × 205 km with ~9.8 m of average slip**, against published Tohoku 2011
source models of ~450 km × ~200 km with ~10 m — the width and slip land
essentially on target.  Wells & Coppersmith is kept as the **fallback** for
locations outside Slab2 coverage, where the interface relations do not apply
anyway.

Slab2 (Hayes et al. 2018) — real fault geometry, and a validity filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The remaining parameters — the depth, strike and dip of the interface *at the
clicked location* — come from **Slab2** (`Hayes et al. 2018,
doi:10.1126/science.aat4723 <https://doi.org/10.1126/science.aat4723>`__), the
USGS global model of subduction-zone geometry, distributed as 0.05°-spaced
regular-grid rasters (depth, dip and strike grids per zone).  A reader was
written for it (``src/io/Slab2Reader.cpp``, plus a wasm-safe query path), with
automatic first-run download of the ~140 MB ScienceBase archive.

Slab2 is used in **two** ways, and the second was the more interesting
realisation:

* **As a parameter source.** The fault is oriented along the real slab
  interface instead of a guessed strike/dip, so a source placed off Honshu
  automatically gets the Japan Trench geometry and one placed off Chile gets
  the Peru–Chile Trench geometry.
* **As a plausibility filter.** Slab2 stores *NaN* wherever a cell lies outside
  a modelled slab.  That doubles as a validity test at zero extra cost: a
  magnitude-9 source requested in the middle of the Atlantic samples NaN and is
  rejected. The application cannot be used to place physically impossible
  megathrust earthquakes on passive margins or abyssal plains.

The same coverage information is rendered back to the user: the globe and the
region view both draw a depth-graded Slab2 overlay, so it is visible *before*
clicking where a great earthquake can plausibly occur.

Tanioka & Satake (1996) — the trench effect
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The standard simplification is to use only :math:`u_z` and ignore the
horizontal seafloor motion, on the grounds that sliding a flat seabed sideways
displaces no water.  **On a steep slope that argument fails.**

`Tanioka & Satake (1996) <https://doi.org/10.1029/96GL00736>`__ showed that
horizontal motion of a *sloping* seafloor contributes an effective vertical
displacement.  A rigid horizontal shift :math:`(u_x, u_y)` moves the material
that used to be at :math:`(x - u_x,\, y - u_y)` to :math:`(x, y)`, so the
elevation seen at the fixed point :math:`(x, y)` changes, to first order, by
:math:`-(u_x \partial_x b + u_y \partial_y b)`:

.. math::

   u_z^{\text{eff}} = u_z - \left(u_x \frac{\partial b}{\partial x}
                              + u_y \frac{\partial b}{\partial y}\right)

This matters precisely where the interesting earthquakes are.  Tohoku and Chile
both rupture next to a deep-sea trench — the steepest bathymetry on Earth — and
there the correction is not a rounding error.  It is implemented in
``effectiveVerticalDisplacement()`` and applied in both the static preview and
the live simulation, with the bathymetry gradient taken from central
differences on the structured GEBCO grid (one-sided at the edges).

The design payoff is that the correction folds the horizontal components back
into a **single effective vertical number per point**, so the solver's
initial-condition interface never had to change: it still consumes one vertical
displacement per cell, exactly as it did with the Gaussian.

The correction is unit-tested independently of the Okada cross-check, since it
is a pure vector-calculus identity rather than an Okada-specific formula: zero
horizontal motion or flat seafloor must be a no-op, up-slope motion must raise
the fixed point and down-slope motion must lower it, with the magnitude given
by the gradient.

.. figure:: ../_images/individual_phase/displacement.png
   :alt: Okada seafloor displacement field
   :width: 85%

   The computed displacement field: uplift over the shallow part of the fault,
   subsidence behind it — the dipole structure a Gaussian cannot represent.

Honest assessment of the source model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Being able to state the limits of a model is part of building it.

* **Scaling laws are weakest exactly where tsunamis are strongest.** Since 1900
  there have been only **five** earthquakes of :math:`M_w \geq 9`.  No
  regression calibrated on a handful of events can be precise there.  Concretely:
  Chile/Maule (:math:`M_w` 8.8) comes out at ~469 km against a published ~450 km
  — very good; Tohoku (:math:`M_w` 9.1, at the extreme edge of the calibration
  range) comes out at 702 km against ~450 km, an overestimate of ~55 %.
* **Uniform slip on a single rectangle.** Real ruptures are heterogeneous, with
  slip concentrated in asperities.  A single uniform-slip rectangle smooths that
  out.
* **Fixed rake and Poisson ratio.** Rake is assumed 90° (pure thrust) and
  :math:`\nu = 0.25` (a Poisson solid).  Both are reasonable defaults for
  megathrust events and both are simplifications.
* **Instantaneous rupture.** The Okada field is *static* and is applied as a
  step change at :math:`t = 0`.  This is well justified: rupture duration
  (seconds to minutes) is short against basin-crossing travel time (hours), and
  the assumption is consistent with the incompressible, hydrostatic
  shallow-water initial condition.
* **Elastic half-space.** No layering, no bathymetric loading, no water column
  in the elastic problem.

Literature and data sources used
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

===================================  ================================================  ===============================================
Source                               Used for                                          Reference
===================================  ================================================  ===============================================
Okada (1985)                         Surface deformation; horizontal :math:`u_x,u_y`   `BSSA 75(4), 1135–1154 <https://doi.org/10.1785/BSSA0750041135>`__
Okada (1992)                         Vertical :math:`u_z`, singular-case handling      `BSSA 82(2), 1018–1040 <https://doi.org/10.1785/BSSA0820021018>`__
Wells & Coppersmith (1994)           Fallback scaling outside Slab2 coverage           `BSSA 84(4), 974–1002 <https://doi.org/10.1785/BSSA0840040974>`__
Strasser, Arango & Bommer (2010)     Interface rupture length and width                `SRL 81(6), 941–950 <https://doi.org/10.1785/gssrl.81.6.941>`__
Tanioka & Satake (1996)              Sloping-seafloor correction                       `GRL 23(8), 861–864 <https://doi.org/10.1029/96GL00736>`__
Hayes et al. (2018) — Slab2          Interface depth/strike/dip; validity mask         `Science 362, 58–61 <https://doi.org/10.1126/science.aat4723>`__
IPGP ``okada85.m``                   Numerical reference for validation                `deformation-lib <https://github.com/IPGP/deformation-lib>`__
GEBCO 2026 grid                      Global bathymetry (15 arc-second)                 BODC/CEDA
USGS Slab2 grid distribution         Slab2 raster data                                 USGS ScienceBase
===================================  ================================================  ===============================================

Part III — Coupling the source to a live solver
-------------------------------------------------

Threading architecture
~~~~~~~~~~~~~~~~~~~~~~~~

The solver cannot run on the render thread — a single time step of a
1000 × 1000 grid is far longer than a frame budget, and blocking the render
loop would freeze the camera.  ``SolverThread`` runs ``WavePropagation2d`` on
its own thread and publishes each completed state into a double-buffered
``SimBuffer``; the renderer swaps to the newest complete frame whenever one is
ready and otherwise redraws the previous one.  The mutex is held only for the
pointer swap, so the solver is never blocked by rendering and the renderer is
never blocked by the solver.  Simulated time and step count travel alongside
the cell data so the HUD can report live progress and an effective time-lapse
factor.

Displacement requests cross the same boundary safely: a click never writes into
the solver directly, it sets an atomic request that the solver thread consumes
at the top of its next iteration.

Stability work
~~~~~~~~~~~~~~~~

Running the solver continuously and unattended for tens of thousands of steps
exposed failure modes that a fixed-length batch run never reaches:

* **CFL re-derivation.** The stable time step must be recomputed from the
  current maximum wave speed on *every* step, not fixed at start-up — a
  displacement injected mid-run changes it immediately.
* **Dry-cell CFL collapse.** The visualization's time-lapse factor collapsed
  from ×90 to ×7 as soon as the wave reached a coast.  Cause: drying cells keep
  their leftover momentum frozen, and computing :math:`u = hu/h` on a nearly
  zero :math:`h` yields an enormous apparent velocity that then throttles
  ``dt`` for the entire rest of the run.  Fixed on both sides — the wave-speed
  scan skips cells the solver treats as dry, and the clamp phase flushes
  ``hu``/``hv`` in dry cells.
* **Froude limiting.** At the wet/dry front a thin water column can still
  produce a non-physical velocity spike.  Capping the velocity at
  :math:`\mathrm{Fr}_{\max}\sqrt{gh}` (with :math:`\mathrm{Fr}_{\max} = 4`)
  removes it from the dynamics without touching genuine flow, and keeps long
  runs from stalling.
* **Non-finite recovery.** Cells that go non-finite are reset to dry rather
  than being allowed to poison the whole grid.

Performance
~~~~~~~~~~~~~

Interactivity is a performance requirement, so the solver kernel was
re-optimised: ``FWave::netUpdates`` was rewritten branch-free (wet/dry handling
and wave accumulation via selects) and force-inlined; the X-sweep was decoupled
through per-thread edge buffers so consecutive edges no longer overlap on cell
``ix+1``, which together with ``__restrict`` pointers let all sweep loops
auto-vectorize; and five grid passes were fused into three.

Result: **10.9 → 5.0 ns** per cell and iteration single-threaded, and
**3.6 → 1.15 ns** with 10 threads — roughly a 3× speed-up (Apple M4,
``DamBreak2d``, 1000 × 1000).

.. raw:: html

   <video width="85%" controls loop muted playsinline>
     <source src="../_static/individual_phase/live_simulation.mp4" type="video/mp4">
     Your browser does not support the video tag.
   </video>
   <p><em>Live simulation: the wave surface is updated every rendered frame,
   with the solver running continuously in the background.</em></p>

Part IV — Taking it to the browser
------------------------------------

Once the desktop application was complete, the project was extended in a
direction the original plan did not contain: **the whole thing was ported to
the web**.  A desktop binary requiring a 7.5 GB GEBCO download, a C++
toolchain and an OpenGL 3.3 context reaches almost nobody; a URL reaches
everybody.  The same C++ solver, the same displacement models and the same
renderer now run inside a browser tab.

WebAssembly and WebGL2
~~~~~~~~~~~~~~~~~~~~~~~~

The C++ core is compiled with **Emscripten** to WebAssembly, and OpenGL calls
map onto **WebGL2**.  Both the solver's OpenMP-style parallelism and the
parallel displacement build use real **pthreads**, which in the browser means
``SharedArrayBuffer`` and therefore cross-origin isolation.  Several details
had to be solved rather than configured:

* **GLSL translation.** The shaders were written as ``#version 330 core``;
  WebGL2 accepts only GLSL ES 300, which is feature-equivalent for everything
  used here but demands explicit default precision qualifiers.  Rather than
  fork the shader files, ``Shader::toGlslEs300()`` rewrites the version
  directive and injects the precision declarations at load time — one code
  path, both targets.
* **Fixed heap.** Memory growth combined with pthreads left the GL bindings
  holding stale heap views mid-boot; every ``glBufferData`` during a growth
  burst failed with ``INVALID_VALUE``.  The build therefore pins a fixed
  1.5 GiB heap, which is the recommended pthreads configuration anyway.
* **Cross-origin isolation on static hosting.** GitHub Pages cannot send the
  COOP/COEP headers that ``SharedArrayBuffer`` requires, so the page retrofits
  them at runtime through a vendored service worker, at the cost of one
  automatic reload on a first visit.
* **embind API.** Roughly 40 functions are exported to JavaScript, covering
  scenario loading, region selection, magnitude commit, station management,
  view toggles and simulation control.

Data pipeline
~~~~~~~~~~~~~~~

The desktop version read GEBCO directly as NetCDF hyperslabs from a 7.5 GB
local file.  A browser cannot, so ``tools/make_web_data.py`` pre-extracts
everything into a compact custom binary format (``TLB1``: magic, dimensions,
bounding box, ``int16`` elevations, gzipped):

* one **globe grid** at ~0.083° for the world view;
* five **high-resolution scenario grids**, one per historical earthquake,
  at native 15 arc-second resolution;
* the world **tiled at 6°** (60 × 30 = 1800 tiles at ~45 arc-seconds), fetched
  on demand so a free-hand selection anywhere on Earth loads only the tiles it
  actually covers — a few hundred MB of static assets instead of the 5–7 GB a
  fully native tiled world would require.

Frontend
~~~~~~~~~~

The desktop ImGui sidebar was replaced by a **React 19 + Vite + Tailwind +
shadcn/ui** frontend (~2,000 lines of TypeScript/TSX) rendering over the
WebGL canvas: region, source, scenario, station and history panels, gauge
time-series charts, hover tooltips and a live HUD.  The application was then
made genuinely usable on phones — a responsive layout for narrow screens, a
mobile dock, and full touch input with rotate, pinch-zoom, pan and tap
gestures.

The shipped WebAssembly bundle is **343 KB of wasm plus 307 KB of JS glue** —
the entire finite-volume solver, the Okada model and the renderer.

Deployment
~~~~~~~~~~~~

Every push to ``main`` triggers a GitHub Actions workflow that regenerates the
data grids when the tooling changes (downloading GEBCO and Slab2 on a cache
miss, after freeing ~12 GB of runner disk space to fit the 4.3 GB archive and
the 7.5 GB grid), builds the frontend, builds this Sphinx documentation, mounts
it under ``/docs``, and publishes everything to GitHub Pages.

Part V — Engineering infrastructure
-------------------------------------

Work that does not show up in a screenshot but without which the rest does not
hold together:

* **Build-system migration.** SCons was replaced by **CMake** to integrate the
  OpenGL dependencies and, later, the Emscripten toolchain.  Git submodules
  were then replaced by ``FetchContent`` with commit-pinned dependencies, so a
  fresh clone needs nothing but CMake.
* **Reproducible environments.** ``nix-shell`` was replaced by a Docker Compose
  setup with separate services for the wasm build, data generation, frontend
  build, static serving and documentation — each step consuming the previous
  step's artefacts.
* **Test suite.** **76 test cases across ~3,050 lines** of Catch2 tests,
  including the Okada reference cross-check, the Tanioka–Satake identity, the
  subduction scaling relations, the ``SolverThread``/``SimBuffer`` handoff and
  the dry-cell CFL regressions.
* **CI.** Every push runs clang-format style checking, cppcheck static
  analysis, the unit tests, sanitizer builds (ASan/UBSan) and Valgrind memory
  checks; pre-commit hooks run style and tests locally.

Effort in numbers
-------------------

===========================================================  ==============
Component                                                    Lines
===========================================================  ==============
Renderer, C++ (``src/visualization``)                        2,823
GLSL shaders (11 hand-written files)                         256
Displacement models and scaling laws                         701
Displacement unit tests                                      326
Slab2 reader and wasm-safe query path                        529
WebAssembly bridge and web data loaders (``src/web``)        1,844
React frontend (TypeScript / TSX)                            2,024
Data extraction tooling (Python)                             404
**Test suite total** (76 Catch2 test cases)                  **3,054**
===========================================================  ==============

Across the individual phase (17 June – 27 July 2026):

* **134 commits**
* **~15,000 lines added / ~4,900 removed** in source, build and documentation
  files (excluding generated artefacts and datasets)
* **9 peer-reviewed sources and datasets** read and applied
* **81 numerical configurations** cross-validated against an external reference
  implementation
* three complete application stages: desktop OpenGL → physical source model →
  browser deployment

What we would do differently
------------------------------

* **Read the calibration range before implementing the formula.** Wells &
  Coppersmith was implemented first and only afterwards read closely enough to
  discover that it explicitly excludes the earthquake class the project is
  about.  The implementation survived as a fallback, but the detour cost time.
* **Validate against an external reference earlier.** The Okada port was
  cross-checked against ``okada85.m`` only after it was already in use.  Doing
  it first would have made every subsequent change safe by construction.
* **Design for the web from the start.** The port was smooth mainly by luck:
  the renderer happened to use only GL 3.3 core features that map onto WebGL2,
  and the solver happened to have no I/O in its hot loop.  Some of the harder
  problems (fixed heap, GLSL dialect) would have been free had the browser been
  a target from day one.

Individual contributions
--------------------------

- **Mika Brückner:** seafloor displacement in full — literature study
  (Okada 1985/1992, Wells & Coppersmith 1994, Strasser et al. 2010, Tanioka &
  Satake 1996, Slab2), ``GaussianDisplacement`` and its replacement by the
  Okada model, horizontal components :math:`u_x/u_y`, validation against
  ``okada85.m``, ``SubductionScaling`` / ``WellsCoppersmith`` / ``OkadaFactory``,
  the Slab2 reader with first-run download, the Tanioka–Satake correction in
  preview and live simulation, click-to-place quakes driven by location and
  magnitude with the subduction overlay, and the associated unit tests; solver
  stability fixes (dry-tolerance threshold in ``FWave``, per-step CFL
  re-derivation, non-finite cell recovery, initial water-depth flooring);
  OpenMP parallelization and NUMA-aware initialization from the course phase;
  project plan, pitch and final presentation; mobile/responsive fixes and the
  Sphinx docs deployment; this report.
- **Jan Vogt:** GEBCO auto-download and native-resolution region reader; the 3D
  terrain renderer with live vertical exaggeration, screen-space hill shading
  and sea-level plane; ``SolverThread`` / ``SimBuffer``; jet and hypsometric
  colormaps and the stacked legends; mesh LOD, frustum culling and MSAA; the
  complete WebAssembly + WebGL2 port; the React + Vite + shadcn/ui frontend;
  Docker Compose environments, CMake ``FetchContent`` migration, data tooling
  and the GitHub Pages deployment; responsive layout and touch input; Froude
  limiting.
- **Yannik Köllmann:** CMake build integration and the OpenGL foundation
  (window, camera, shaders); the globe view with mouse-driven region selection;
  keyboard shortcuts and resolution controls; shader extraction into separate
  files and ``Shader.h``; 2D solver bug fixes; time metadata and HUD;
  GUI-thread CPU pinning (``pinThreadToCore``, ``TSUNAMI_VIZ_CORE``); the
  branchless/vectorized ``FWave`` kernel, fused sweep passes and the dry-cell
  CFL fix; gauge stations.
