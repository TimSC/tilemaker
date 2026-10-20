# Installing tilemaker-core

tilemaker-core reads its input with [cppo5m](https://github.com/TimSC/cppo5m), a git
submodule. Clone with submodules, or fetch them afterwards:

    git submodule update --init --recursive

### Ubuntu and Debian

Install the dependencies:

    sudo apt install build-essential cmake liblua5.1-0-dev libsqlite3-dev libshp-dev \
      libboost-program-options-dev libboost-filesystem-dev libboost-system-dev \
      rapidjson-dev libexpat1-dev zlib1g-dev libprotobuf-dev protobuf-compiler

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
