###########
Tsunami Lab
###########

Repository is hosted under https://github.com/ykoellmann/tsunami_lab.

User Documentation is hosted under https://ykoellmann.github.io/tsunami_lab/.

Code Documentation is hosted under https://ykoellmann.github.io/tsunami_lab/doxygen/.

Project Team:

https://github.com/JanVogt06

https://github.com/mbbrueckner

https://github.com/ykoellmann

***********************
Development environment
***********************

The dev environments are containerised via ``docker-compose.yml``
(replaces the former ``shell.nix``)::

    docker compose run --rm native      # build solver CLI + run unit tests
    docker compose run --rm web-build   # build the wasm/WebGL2 bundle
    docker compose run --rm data        # extract demo grids from data/GEBCO_2026.nc
    docker compose run --rm frontend    # build the React app into web/dist
    docker compose run --rm docs        # build the sphinx documentation
    docker compose up serve             # serve web/dist at http://localhost:8080

Frontend development (React + Vite + shadcn/ui, in ``web/``)::

    npm install && npm run dev          # dev server at http://localhost:5173

One-time host setup (the nix shellHook used to do this)::

    git config core.hooksPath .githooks
