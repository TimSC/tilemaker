/*! \file */ 
#ifndef _SHARED_DATA_H
#define _SHARED_DATA_H

#include <vector>
#include <map>

#include "rapidjson/document.h"

#include "osm_store.h"
#include "output_object.h"
#include "mbtiles.h"
#include "tile_data.h"
#include "layer.h"

///\brief Config read from JSON to control behavior of program
class Config
{
public:
	class LayerDefinition layers;
	uint baseZoom, startZoom, endZoom;
	uint mvtVersion;
	bool includeID, compress, gzip, combineSimilarObjs;
	std::string compressOpt;
	bool hasClippingBox;
	double minLon, minLat, maxLon, maxLat;
	std::string projectName, projectVersion, projectDesc;
	std::string defaultView;

	Config();
	virtual ~Config();

	void readConfig(rapidjson::Document &jsonConfig, bool &hasClippingBox, Box &clippingBox);
};

///\brief Data used by worker threads ::outputProc to write output
class SharedData
{
public:
	uint zoom;

	///Number of worker threads to create
	int threadNum;
	class TileData &tileData;
	const class LayerDefinition &layers;
	bool verbose;
	bool sqlite;
	MBTiles mbtiles;
	std::string outputFile;

	const class Config &config;

	SharedData(const class Config &configIn, const class LayerDefinition &layers,
		class TileData &tileData);
	virtual ~SharedData();
};

#endif //_SHARED_DATA_H

