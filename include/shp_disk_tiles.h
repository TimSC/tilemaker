/*! \file */ 
#ifndef _SHP_DISK_TILES
#define _SHP_DISK_TILES

#include <thread>         // std::thread
#include <mutex>          // std::mutex
#include <memory>
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

	virtual bool GetAvailableTileExtent(Box &clippingBox) {return false;};

	virtual void Load(class LayerDefinition &layers, 
		bool hasClippingBox,
		const Box &clippingBox);

private:

	std::vector<uint> verifyIntersectResults(std::vector<IndexValue> &results, Point &p1, Point &p2) const;

	//These must not be modified once processing has started. Multi-threading is used.
	const class LayerDefinition &layers;
	class TileIndexCached tileIndex;
	class BareTileIndex bareTileIndex;
	const uint baseZoom;
	Box clippingBox;
	int xMin, xMax, yMin, yMax;

	//The following must be locked when used in multi threading
	std::mutex mtx;
	///Each thread must have its own shapelib objects.
	std::map<std::thread::id, std::map<int, std::shared_ptr<class ShapefileReader> > > shapefileReaderThreadMap;
};

#endif //_OSM_DISK_TILES

