/*! \file */ 
#ifndef _SHP_DISK_TILES
#define _SHP_DISK_TILES

#include "tile_data.h"
#include "shared_data.h"

class ShpDiskTiles : public TileDataSource
{
public:
	ShpDiskTiles(uint baseZoom, const class LayerDefinition &layers);

	virtual void GenerateTileListAtZoom(uint zoom, TileCoordinatesSet &dstCoords);

	virtual void GetTileData(TileCoordinates dstIndex, uint zoom, 
		std::vector<OutputObjectRef> &dstTile);

	// Find intersecting shapefile layer
	virtual std::vector<std::string> FindIntersecting(const std::string &layerName, Box &box) const;
	virtual bool Intersects(const std::string &layerName, Box &box) const;

	virtual uint GetBaseZoom();

	virtual void Load(class LayerDefinition &layers, 
		bool hasClippingBox,
		const Box &clippingBox);

private:

	std::vector<uint> verifyIntersectResults(std::vector<IndexValue> &results, Point &p1, Point &p2) const;

	const class LayerDefinition &layers;
	class TileIndexCached tileIndex;
	class BareTileIndex bareTileIndex;
	const uint baseZoom;
	Box clippingBox;
	int xMin, xMax, yMin, yMax;
};

#endif //_OSM_DISK_TILES

