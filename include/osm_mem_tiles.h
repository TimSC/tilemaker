/*! \file */ 
#ifndef _OSM_MEM_TILES
#define _OSM_MEM_TILES

#include "tile_data.h"

/**
	\brief OsmMemTiles stores the output geometries of OSM objects in memory and provides a vector of OutputObjects for specified tiles

	OsmLuaProcessing sends it the geometries the Lua profile writes. Every
	geometry is stored (materialized) here; none are rebuilt from the OSM data.
*/
class OsmMemTiles : public TileDataSource {

public:
	OsmMemTiles(size_t threadNum, uint indexZoom, bool includeID)
		: TileDataSource(threadNum, indexZoom, includeID) {}

	std::string name() const override { return "osm"; }

	void Clear();
};

#endif //_OSM_MEM_TILES
