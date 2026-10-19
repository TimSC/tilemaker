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

### Docker

Build from the project root directory, with the submodules fetched:

    docker build . -t tilemaker-core

It can also be built with a `BUILD_DEBUG` build argument, which will build the executable for
Debug, and not strip out symbols. `gdb` will also be installed to facilitate debugging:

    docker build . --build-arg BUILD_DEBUG=1 -t tilemaker-core

The container can be run like this:

    docker run -it --rm -v $(pwd):/data tilemaker-core --input /data/area.osm --output /data/area.pmtiles

The tilemaker-server can be run like this:

    docker run -it --rm -v $(pwd):/data -p 8080:8080 --entrypoint /usr/src/app/tilemaker-server tilemaker-core /data/area.mbtiles

Map the volume your input files are in to a path within the container, as in the example
above. To read a map database from the container, give `--db` a connection string with a host
the container can reach.
