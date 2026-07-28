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

Code Documentation
==================

The source code documentation is generated using Doxygen and hosted online:

- `Doxygen Documentation <https://ykoellmann.github.io/tsunami_lab/doxygen/>`_

Build Process
=============

The project uses `SCons <https://scons.org/>`_ as its build tool and
`Nix <https://nixos.org/>`_ to provide a reproducible development environment.

**Setup (first time):**

.. code-block:: bash

   curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix | sh -s -- install

Then restart your terminal.

**Local build:**

.. code-block:: bash

   nix-shell
   scons mode=debug
   ./build/tests

**Style check:**

.. code-block:: bash

   find src/ -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror

To fix style violations automatically:

.. code-block:: bash

   find src/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i

**CI/CD:**

Every push and pull request to ``main`` triggers the GitHub Actions pipeline, which runs
style checking (clang-format), static analysis (cppcheck), unit tests, sanitizer builds,
and Valgrind memory checks.
