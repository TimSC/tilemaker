/*! \file */
#ifndef _OSM_AREA_PROCESSOR_H
#define _OSM_AREA_PROCESSOR_H

#include "osm_area.h"
#include "significant_tags.h"

class OSMStore;
class OsmLuaProcessing;

/**
 *\brief Runs the OSM data of an area through the Lua profile, which sends the
 * output to OsmMemTiles.
 *
 * Relations are scanned first, then nodes, ways and relations are processed in
 * turn, as tilemaker always has. Everything is kept in OSMStore, since the
 * input is small; it all happens on one thread.
 */
void ProcessOsmArea(
	const OsmArea& area,
	OSMStore& osmStore,
	OsmLuaProcessing& output,
	const SignificantTags& nodeKeys,
	const SignificantTags& wayKeys
);

#endif //_OSM_AREA_PROCESSOR_H
