/*! \file */ 
#ifndef _OSM_DISK_TILES
#define _OSM_DISK_TILES

#include <fstream>
#include <mutex>
#include "tile_data.h"
#include "osm_store.h"
#include "../cppGzip/SeekableTar.h"
#include "../cppGzip/DecodeGzip.h"

bool CheckAvailableDiskTileExtent(const std::string &basePath,
	Box &clippingBox);

/**
 * Collection of tile at various zooms
 */
class OsmDiskTilesZoom
{
public:
	OsmDiskTilesZoom() {};
	virtual ~OsmDiskTilesZoom() {};

	virtual void GetAvailable(uint &tilesZoom,
		bool &tileBoundsSet,
		int &xMin, int &xMax, int &yMin, int &yMax) {};

	///This must be thread safe!
	virtual void GetTile(uint zoom, int x, int y, class IDataStreamHandler *output) {};
};

/**
 * Reading nested folders of pbf tiles
 */
class OsmDiskTilesZoomDir : public OsmDiskTilesZoom
{
private:
	std::string basePath;
	uint tilesZoom;
	bool tileBoundsSet;
	int xMin, xMax, yMin, yMax;

public:
	OsmDiskTilesZoomDir(std::string pth);
	virtual ~OsmDiskTilesZoomDir();

	virtual void GetAvailable(uint &tilesZoom,
		bool &tileBoundsSet,
		int &xMin, int &xMax, int &yMin, int &yMax);

	///This must be thread safe!
	virtual void GetTile(uint zoom, int x, int y, class IDataStreamHandler *output);
};

/**
 * Reading a tar of gzipped columns of pbfs
 */
class OsmDiskTilesZoomTar : public OsmDiskTilesZoom
{
private:
	std::string tarPath;
	uint tilesZoom;
	bool tileBoundsSet;
	int xMin, xMax, yMin, yMax;

	///This must be locked to access any of the variables in this section
	std::mutex m;
	std::filebuf infi;
	std::shared_ptr<class SeekableTarRead> seekableTarRead;
	std::map<int, std::shared_ptr<class SeekableTarEntry> > tarEntries;
	std::map<int, std::shared_ptr<class DecodeGzipFastSeek> > colTarDec;
	std::map<int, std::shared_ptr<class SeekableTarRead> > colTarReaders;

public:
	OsmDiskTilesZoomTar(std::string pth);
	virtual ~OsmDiskTilesZoomTar();

	virtual void GetAvailable(uint &tarPath,
		bool &tileBoundsSet,
		int &xMin, int &xMax, int &yMin, int &yMax);

	///This must be thread safe!
	virtual void GetTile(uint zoom, int x, int y, class IDataStreamHandler *output);
};

/**
 * \brief Used by OsmDiskTiles has temporary storage while processing one or more tiles in a lazy fashion.
 */
class OsmDiskTmpTiles : public TileDataSource
{
public:
	OsmDiskTmpTiles(uint baseZoom);

	virtual void GenerateTileListAtZoom(uint zoom, TileCoordinatesSet &dstCoords) {};

	virtual void GetTileData(TileCoordinates dstIndex, uint zoom, 
		std::vector<OutputObjectRef> &dstTile) {};

	virtual void AddObject(TileCoordinates index, OutputObjectRef oo);

	virtual uint GetBaseZoom();

	virtual bool GetAvailableTileExtent(Box &clippingBox, std::string &boxSource);

	class TileIndex tileIndex;

};

/**
	\brief OsmDiskTiles reads OSM objects on disk and provides a vector of OutputObjectRef for specified tiles
	
	The data is read from a set of pbf files. The output objects are sent to OsmMemTiles for storage.
*/
class OsmDiskTiles : public TileDataSource
{
public:
	OsmDiskTiles(const std::string &basePath,
		const class Config &config,
		const std::string &luaFile,
		const class LayerDefinition &layers,	
		const class TileDataSource &shpData);

	///This must be thread safe!
	virtual void GenerateTileListAtZoom(uint zoom, TileCoordinatesSet &dstCoords);

	///This must be thread safe!
	virtual void GetTileData(TileCoordinates dstIndex, uint zoom, 
		std::vector<OutputObjectRef> &dstTile);

	virtual void AddObject(TileCoordinates index, OutputObjectRef oo);

	virtual uint GetBaseZoom();

	virtual bool GetAvailableTileExtent(Box &clippingBox, std::string &boxSource);

private:
	//This variables are generally safe for multiple threads to read, but not to write. (They are const anyway.)

	const std::string basePath;
	const class Config &config;
	const std::string luaFile;
	const class LayerDefinition &layers;
	const class TileDataSource &shpData;

	std::shared_ptr <class OsmDiskTilesZoom> inTiles;
};

#endif //_OSM_DISK_TILES

