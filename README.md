# tilemaker-core

tilemaker-core renders the vector tiles (in Mapbox Vector Tile format) of a small area from its OpenStreetMap data, read from a file or directly from a [pycrocosm](https://github.com/TimSC/pycrocosm) map database. It is a cut-down [tilemaker](https://github.com/systemed/tilemaker): the same Lua tag processing, JSON layer config, geometry handling and tile output, without the machinery for reading whole countries or the planet.

* Input: an `.osm`, `.o5m`, `.pbf` or OSM JSON file covering the area (such as an API map query), read by [cppo5m](https://github.com/TimSC/cppo5m), or the map database's tables for the area.
* Output: `.mbtiles`, `.pmtiles`, or a directory of tiles.
* Zooms: 11 and up. Lower zooms would need far larger areas.
* Everything is held in memory, which is fine for an area a few tiles across.

Upstream tilemaker's PBF reader, its on-disk, sorted and sharded stores and lazy geometries have been removed.

## Getting started

Build it (see [INSTALL.md](docs/INSTALL.md) for dependencies):

    git submodule update --init --recursive
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel

Render an area from a map query:

    curl -o area.osm "http://localhost:8000/api/0.6/map?bbox=-1.095,50.795,-1.085,50.8"
    build/tilemaker --input area.osm --output area.mbtiles

or straight from the map database:

    build/tilemaker --db "dbname=map user=renderer" --bbox -1.095,50.795,-1.085,50.8 --output area.mbtiles

Then, to look at your tiles with the demonstration server:

    cd server
    ../build/tilemaker-server ../area.mbtiles

and open http://localhost:8081/ in a browser. The server can also render each tile from the map database as the map asks for it (`--render`; see [RUNNING.md](docs/RUNNING.md)).

Or do all of this with Docker Compose: see [INSTALL.md](docs/INSTALL.md).

Read about the options in [RUNNING.md](docs/RUNNING.md).

## Coastline and Landcover

To include sea tiles and small-scale landcover, run

    ./get-coastline.sh
    ./get-landcover.sh

This will download coastline and landcover data; you will need around 2GB disk space. They are loaded at startup, for whatever area is being rendered.

Have a look at the coastline and landcover example in the [`resources/`](./resources) directory.

## Your own configuration

Vector tiles contain (generally thematic) 'layers'. For example, your tiles might contain river, cycleway and railway layers. It's up to you what OSM data goes into each layer. You configure this in tilemaker with two files:

* a JSON file listing each layer, and the zoom levels at which to apply it
* a Lua program that looks at each node/way's tags, and places it into layers accordingly

You can read more about these in [CONFIGURATION.md](docs/CONFIGURATION.md).

The JSON configuration and Lua processing files are specified with `--config` and `--process` respectively. Defaults are config.json and process.lua in the current directory. If there is no config.json and process.lua in the current directory, and you do not specify `--config` and `--process`, an error will result.

You might also find these resources helpful:

* Read our [introduction to vector tiles](docs/VECTOR_TILES.md).
* See https://github.com/mapbox/awesome-vector-tiles for a list of renderers which support vector tiles.

## Contributing

Formatting: braces and indents as shown, hard tabs (4sp). Please be conservative about adding dependencies or increasing the memory requirement.

## Copyright

tilemaker-core is a cut-down fork of [tilemaker](https://github.com/systemed/tilemaker), which is maintained by Richard Fairhurst and supported by [many contributors](https://github.com/systemed/tilemaker/graphs/contributors). We particularly celebrate the invaluable contributions of Wouter van Kleunen, who passed away in 2022.

Copyright tilemaker contributors, 2015-2025.

The tilemaker code is licensed as FTWPL; you may do anything you like with this code and there is no warranty.

Licenses of third-party libraries:

- [cppo5m](https://github.com/TimSC/cppo5m) is licensed under BSD, and its iso8601lib under MIT
- [kaguya](https://github.com/satoren/kaguya) is licensed under the Boost Software Licence
- [libdeflate](https://github.com/ebiggers/libdeflate/) is licensed under MIT
- [libpopcnt](https://github.com/kimwalisch/libpopcnt) is licensed under BSD 2-clause
- [minunit](https://github.com/siu/minunit) is licensed under MIT
- [pmtiles](https://github.com/protomaps/PMTiles) is licensed under BSD 3-clause
- [polylabel](https://github.com/mapbox/polylabel) is licensed under ISC
- [Simple-Web-Server](https://gitlab.com/eidheim/Simple-Web-Server) is licensed under MIT
- [protozero](https://github.com/mapbox/protozero) is licensed under BSD 2-clause
- [sqlite_modern_cpp](https://github.com/SqliteModernCpp/sqlite_modern_cpp) is licensed under MIT
- [visvalingam.cpp](https://github.com/felt/tippecanoe/blob/main/visvalingam.cpp) is licensed under MIT
- [vtzero](https://github.com/mapbox/vtzero) is licensed under BSD 2-clause
