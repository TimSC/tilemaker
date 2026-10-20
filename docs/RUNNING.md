# Running tilemaker-core

You can get a run-down of available options with

    tilemaker --help

The following document explains each option.

## What it does

tilemaker-core renders the vector tiles of one area, from the config's minzoom to its maxzoom.
Its input is the OpenStreetMap data of that area: the objects in it and around it, as a map
query returns them. It holds the whole input in memory, so the area should be small: a few
tiles across, not a country. A low zoom's tiles cover far more than that, so the input should
be summarised for them (pycromap does this); or set the config's minzoom to leave them out.

## From a file

    tilemaker --input area.osm \
              --output area.mbtiles \
              --config resources/config-openmaptiles.json \
              --process resources/process-openmaptiles.lua

The input can be `.osm`, `.o5m`, `.pbf` or `.json` (OSM JSON), read by
[cppo5m](https://github.com/TimSC/cppo5m); gzipped files are not read. A pycrocosm or
OpenStreetMap API map query gives a suitable file:

    curl -o area.osm "http://localhost:8000/api/0.6/map?bbox=-1.095,50.795,-1.085,50.8"

The area rendered is `--bbox` if given, otherwise the bounds the file declares, otherwise
those of its nodes. You can give `--input` more than once; the files are read together.

The `--config` and `--process` arguments are the paths of your JSON config file and Lua
processing script. These are described in CONFIGURATION.md. Here we're using the ready-made
OpenMapTiles-compatible script.

You can output to an .mbtiles or .pmtiles file, or write tiles directly to the filesystem by
specifying a directory path for `--output`.

## One tile

`--tile z/x/y` renders just that tile, for example to make tiles as they are asked for:

    tilemaker --input tile.o5m --tile 14/8142/5499 --output tiles

The input should cover the tile and a margin of an eighth of it on each side, so that labels and
lines crossing its edges are included; `--bbox` is not needed. Write it to a directory: the tile is at
`tiles/14/8142/5499.pbf`, gzipped unless the config's `compress` setting says otherwise, and is
missing if there was nothing to draw. The tile can be at any zoom no higher than the config's
maxzoom.

Without `--tile`, tilemaker renders every tile of the area, from the config's minzoom to its
maxzoom.

## Performance

tilemaker-core runs on one thread: a single tile has nothing to share between threads, and an
small area's tiles take seconds. To render several tiles at once, run several tilemakers.

## Output messages

Running tilemaker with the `--verbose` argument will output any issues encountered during tile
creation.

Running tilemaker with the `--quiet` argument will suppress anything written to stdout during
tile creation. stderr is unaffected.

You may see geometry errors reported by Boost::Geometry. This typically reflects an error
in the OSM source data (for example, a multipolygon with several inner rings but no outer ring).

Ways missing some of their nodes are skipped, and counted in a message. A map query includes
every node of the ways it returns, so this suggests the input was clipped some other way. You
can tell tilemaker to use what nodes there are with `--skip-integrity`.
