# Running tilemaker-core

You can get a run-down of available options with

    tilemaker --help

The following document explains each option.

## What it does

tilemaker-core renders the vector tiles of one area, from z11 up to the config's maxzoom. Its
input is the OpenStreetMap data of that area: the objects in it and around it, as a map
query returns them. It holds the whole input in memory, so the area should be small: a few
tiles across, not a country. Lower zooms would need far larger areas, so the config's minzoom
is raised to 11.

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

## From the map database

tilemaker-core can read a pycrocosm map database directly, without going through the API:

    PGHOST=localhost PGDATABASE=map PGUSER=renderer \
    tilemaker --db \
              --bbox -1.095,50.795,-1.085,50.8 \
              --output area.mbtiles \
              --config resources/config-openmaptiles.json \
              --process resources/process-openmaptiles.lua

`--db` alone connects with the libpq `PG*` environment variables; it can also take a libpq
connection string, such as `--db "host=localhost dbname=map user=renderer"`. Keep the password
in `PGPASSWORD` or `~/.pgpass` rather than on the command line. A role that can only read is
enough: everything is read in one read-only transaction.

`--table-prefix` names the table set to read, by default `pycrocosm_mod_` (pgmap's
`dbtablemodifyprefix`). Its views show the edits over the imported planet, so this is the
current map.

The map must store way and relation bounding boxes, which is how the ways and relations
reaching into the area are found. Map schema versions 15 and 16 are supported; anything else
is refused.

If the map keeps the shape cache (pgmap's `way_shapes` and `relation_shapes`), ways and
relations take their shapes from it. Without it, ways are built from their nodes, fetching
those outside the area by ID, and multipolygons whose ways reach outside the area have those
ways fetched too, so that they are whole. Other relations, such as boundaries, are built from
the ways in the area only.

## One tile

`--tile z/x/y` renders just that tile, for example to make tiles as they are asked for:

    tilemaker --db --tile 14/8142/5499 --output tiles

The area read is the tile and a margin of an eighth of it on each side, so that labels and lines
crossing its edges are included; `--bbox` is not needed. Write it to a directory: the tile is at
`tiles/14/8142/5499.pbf`, gzipped unless the config's `compress` setting says otherwise, and is
missing if there was nothing to draw. The tile must be z11 or higher, and no higher than the
config's maxzoom.

Without `--tile`, tilemaker renders every tile of the area, from z11 to maxzoom.

## The demonstration server

`tilemaker-server` shows tiles on a map in a browser. It serves an .mbtiles:

    cd server
    tilemaker-server area.mbtiles

or, with `--render`, renders each tile from the map database as the map asks for it, by running
tilemaker with `--tile` (the program beside it, unless `--tilemaker` says otherwise):

    cd server
    tilemaker-server --render --bbox -1.095,50.795,-1.085,50.8 \
                     --config ../resources/config-openmaptiles.json \
                     --process ../resources/process-openmaptiles.lua

`--bbox` is where the map opens. The database is given as for tilemaker (`--db`, or the `PG*`
environment variables, and `--table-prefix`). `--threads` tiles are rendered at once (4 by
default), each allowed `--timeout` seconds (60). Tiles below z11 are empty. Each tile is
rendered afresh, so the map shows edits as soon as they are made; the config, Lua and any
shapefiles are loaded again for each tile, so keep large shapefiles out of the config.

The map is at http://localhost:8081/ (`--port` to change it).

## Filtering

You can tell tilemaker to only look at objects with certain tags. If you're making a thematic
map, this allows tilemaker to skip data it won't need. Specify this in your Lua file like one
of these three examples:

    -- Only include major roads
    way_keys = {"highway=motorway", "highway=trunk", "highway=primary", "highway=secondary"}

    -- Only include railways
    way_keys = {"railway"}

    -- Include everything but not buildings
    way_keys = {"~building"}

## Merging

You can use the `--merge` switch to add to an existing .mbtiles, for example tiles of a
neighbouring area, or low zoom tiles made another way. Any OSM objects which appear in both
will be written twice.

## Output messages

Running tilemaker with the `--verbose` argument will output any issues encountered during tile
creation, and with `--db`, the time each query took.

Running tilemaker with the `--quiet` argument will suppress anything written to stdout during
tile creation. stderr is unaffected.

You may see geometry errors reported by Boost::Geometry. This typically reflects an error
in the OSM source data (for example, a multipolygon with several inner rings but no outer ring).

Ways missing some of their nodes are skipped, and counted in a message. A map query includes
every node of the ways it returns, so this suggests the input was clipped some other way. You
can tell tilemaker to use what nodes there are with `--skip-integrity`.
