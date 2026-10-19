# Installing tilemaker-core

tilemaker-core reads its input with [cppo5m](https://github.com/TimSC/cppo5m), a git
submodule. Clone with submodules, or fetch them afterwards:

    git submodule update --init --recursive

### Ubuntu and Debian

Install the dependencies:

    sudo apt install build-essential cmake liblua5.1-0-dev libsqlite3-dev libshp-dev \
      libboost-program-options-dev libboost-filesystem-dev libboost-system-dev \
      rapidjson-dev libexpat1-dev zlib1g-dev libprotobuf-dev protobuf-compiler libpq-dev

LuaJIT can be used instead of Lua: install `libluajit-5.1-dev` and it is found if Lua is not.

### Building

tilemaker-core builds with CMake:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    sudo cmake --install build

Run the tests with:

    ctest --test-dir build

cppo5m is built from the submodule. To build with another copy, pass
`-DCPPO5M_SOURCE_DIR=/path/to/cppo5m`; to use an installed one, pass `-DCPPO5M_SOURCE_DIR=`.

Pass `-DTILEMAKER_BUILD_TESTS=OFF` to build only tilemaker.

### Docker Compose

`docker-compose.yml` builds the image and runs tilemaker and the demonstration server, with
input and output in `./data`. With the submodules fetched:

    docker compose build

Render an area from a file:

    docker compose run --rm tilemaker --input /data/area.osm --output /data/area.mbtiles

or from a pycrocosm map database, using the `PG*` variables from your shell or a `.env` file
beside `docker-compose.yml`:

    docker compose run --rm tilemaker --db --bbox -1.095,50.795,-1.085,50.8 --output /data/area.mbtiles

`PGHOST` defaults to `host.docker.internal`, the machine running Docker; PostgreSQL there must
accept connections from the Docker network.

Then look at the map at http://localhost:8081/:

    docker compose up server

The server renders each tile from the map database as the map asks for it, running tilemaker in
the same container. `TILEMAKER_BBOX` is where the map opens, `TABLE_PREFIX` and `MODIFY_PREFIX`
the static and modify tables to read (as pgmap's `dbtableprefix` and `dbtablemodifyprefix`), and
`TILEMAKER_PORT` the port. To serve tiles rendered earlier instead:

    docker compose run --rm --service-ports server --input /data/area.mbtiles --static /usr/src/app/static --port 8081

The build compiles 4 files at once; set `BUILD_JOBS` to change it.

### Docker

Build from the project root directory, with the submodules fetched:

    docker build . -t tilemaker-core

It can also be built with a `BUILD_DEBUG` build argument, which will build the executable for
Debug, and not strip out symbols. `gdb` will also be installed to facilitate debugging:

    docker build . --build-arg BUILD_DEBUG=1 -t tilemaker-core

The container can be run like this:

    docker run -it --rm -v $(pwd):/data tilemaker-core --input /data/area.osm --output /data/area.pmtiles

The tilemaker-server can be run like this:

    docker run -it --rm -v $(pwd):/data -p 8081:8081 --entrypoint /usr/src/app/tilemaker-server tilemaker-core /data/area.mbtiles

Map the volume your input files are in to a path within the container, as in the example
above. To read a map database from the container, give `--db` a connection string with a host
the container can reach.
