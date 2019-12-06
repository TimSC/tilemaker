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
	virtual ~ShpDiskTiles();

	virtual void GetTileData(TileCoordinates dstIndex, uint zoom, 
		std::vector<OutputObjectRef> &dstTile);

	// Find intersecting shapefile layer
	virtual std::vector<std::string> FindIntersecting(const std::string &layerName, Box &box) const;
	virtual bool Intersects(const std::string &layerName, Box &box) const;

	virtual uint GetBaseZoom();

	virtual bool GetAvailableTileExtent(Box &clippingBox, std::string &boxSource);

	virtual void Load(class LayerDefinition &layers, 
		bool hasClippingBox,
		const Box &clippingBox);

private:

	//These must not be modified once processing has started. Multi-threading is used.
	const class LayerDefinition &layers;
	class BareTileIndex bareTileIndex;
	const uint baseZoom;
	Box clippingBox;
	int xMin, xMax, yMin, yMax;

	//The following must be locked when used in multi threading
	std::mutex mtx;
	///Each thread must have its own shapelib objects.
	std::map<std::thread::id, std::map<int, std::shared_ptr<class ShapefileReader> > > shapefileReaderThreadMap;
	std::map<unsigned int, class ShapeFileObject> shapeObjectCache;
};

#endif //_OSM_DISK_TILES

