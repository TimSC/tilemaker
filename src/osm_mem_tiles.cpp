#include "osm_mem_tiles.h"

void OsmMemTiles::Clear() {
	for (auto& entry : objects)
		entry.clear();
	for (auto& entry : objectsWithIds)
		entry.clear();
}
