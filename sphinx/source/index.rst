.. Tsunami Lab documentation master file, created by
   sphinx-quickstart on Thu Apr  9 11:54:44 2026.

Tsunami Lab — Project Reports
===============================

Documentation and reports for the Tsunami Lab at Friedrich Schiller University Jena.

.. toctree::
   :maxdepth: 2
   :caption: Project Phases:

   Overview <self>
   chapters/01_riemann_solver
   chapters/02_finite_volume
   chapters/03_Bathymetry_&_Boundary_Conditions.rst
   chapters/04_two_dimensional
   chapters/05_large_data_io
   chapters/06_tsunami_simulations
   chapters/07_checkpointing
   chapters/08_optimization
   chapters/09_parallelization
   chapters/10_individual_week1
   chapters/11_individual_week2
   chapters/12_individual_week3
   chapters/13_project_report

Live Application
================

The interactive browser version is deployed from ``main`` to GitHub Pages:

- `Live application <https://squadcommit.github.io/Riptide/>`_

Code Documentation
==================

The source code documentation is generated using Doxygen and hosted online:

- `Doxygen Documentation <https://ykoellmann.github.io/tsunami_lab/doxygen/>`_

Build Process
=============

The project builds with `CMake <https://cmake.org/>`_. Third-party
dependencies (Catch2, pugixml, glm) are downloaded at configure time via
``FetchContent``, so a fresh clone needs no submodules. ``docker-compose.yml``
provides reproducible environments for every build step.

**Web app (Docker).** Four steps, in this order, since each consumes the
artifacts of the previous one:

.. code-block:: bash

   docker compose run --rm web-build   # C++ -> wasm     -> web/public/wasm/
   docker compose run --rm data        # GEBCO/Slab2     -> web/public/data/
   docker compose run --rm frontend    # React build     -> web/dist/
   docker compose up serve             # http://localhost:8080

**Native solver and unit tests:**

.. code-block:: bash

   cmake -B build && cmake --build build -j
   ./build/tests            # unit tests
   ./build/tsunami_lab      # batch solver

**Wasm build without Docker** (needs the Emscripten SDK, verified with 6.0.2):

.. code-block:: bash

   emcmake cmake -B build-web && cmake --build build-web -j

**Documentation:**

.. code-block:: bash

   docker compose run --rm docs        # sphinx -> sphinx/build/html

**Style check:**

.. code-block:: bash

   find src/ -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror

To fix style violations automatically:

.. code-block:: bash

   find src/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i

**CI/CD:**

Every push and pull request to ``main`` triggers the GitHub Actions pipeline, which runs
style checking (clang-format), static analysis (cppcheck), unit tests, sanitizer builds,
and Valgrind memory checks. A second workflow deploys the web app and these docs to
GitHub Pages on every push to ``main``.
