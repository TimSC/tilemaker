#include "osm_disk_tiles.h"
#include "osm_lua_processing.h"
#include <boost/filesystem.hpp>
#include <iostream>
#include "../cppo5m/utils.h"
using namespace std;
using namespace boost::filesystem;

string PathTrailing(const path &in)
{
	string out;
	for (const path &pp : in)
		out = pp.string();
	return out;
}

// *****************************************

OsmDiskTilesZoomDir::OsmDiskTilesZoomDir(std::string pth) : OsmDiskTilesZoom()
{
	basePath = pth;
	tilesZoom = 0;
	tileBoundsSet = false;
	xMin = 0; xMax = 0; yMin = 0; yMax = 0;

	// Check for available zoom folders, choose the highest zoom
	path basePath2 (basePath);
	directory_iterator end_itr;
	for (directory_iterator itr(basePath2); itr != end_itr; ++itr)
	{
		if(!is_directory(itr->path()))
			continue;
		auto itr2 = --(itr->path().end()); //Get the folder name
		int zoom = atoi(itr2->c_str());
		if (to_string(zoom) != *itr2)
			continue;
		if (zoom > tilesZoom)
			tilesZoom = zoom;
	}
	if (tilesZoom==0)
		throw runtime_error("No tile zoom folders found");

	// Determine extent of available tile files
	path p (to_string(tilesZoom));
	bool firstDir = true;
	for (directory_iterator itr(basePath2/p); itr != end_itr; ++itr)
	{
		if(!is_directory(itr->path()))
			continue;
		
		int x = std::stoi( PathTrailing(itr->path()) );
		if(!tileBoundsSet || x > xMax)
			xMax = x;
		if(!tileBoundsSet || x < xMin)
			xMin = x;

		if(firstDir)
		{
			path p2 (itr->path());
			for (directory_iterator itr2(p2); itr2 != end_itr; ++itr2)
			{
				if(!is_regular_file(itr2->path())) continue;
				int y = std::stoi( PathTrailing(itr2->path()) );
				if(!tileBoundsSet || y > yMax)
					yMax = y;
				if(!tileBoundsSet || y < yMin)
					yMin = y;		
				tileBoundsSet = true;
			}
		}
		
		firstDir = false;
	}
}

OsmDiskTilesZoomDir::~OsmDiskTilesZoomDir()
{

}

void OsmDiskTilesZoomDir::GetAvailable(uint &tilesZoom,
		bool &tileBoundsSet,
		int &xMin, int &xMax, int &yMin, int &yMax)
{
	tilesZoom = this->tilesZoom;
	tileBoundsSet = this->tileBoundsSet;
	xMin = this->xMin;
	xMax = this->xMax;
	yMin = this->yMin;
	yMax = this->yMax;
}

void OsmDiskTilesZoomDir::GetTile(uint zoom, int x, int y, class IDataStreamHandler *output)
{
	if(zoom != this->tilesZoom)
		throw runtime_error("Tiles can only be accessed at the zoom returned by GetAvailable");

	// ----	Read PBF file

	path inputFile(this->basePath);
	inputFile /= to_string(this->tilesZoom);
	inputFile /= to_string(x); 
	inputFile /= to_string(y) + ".pbf";
	//cout << inputFile << endl;

	std::filebuf infi;
	infi.open(inputFile.string(), std::ios::in);
	LoadFromPbf(infi, output);
}

// ********************************************

OsmDiskTmpTiles::OsmDiskTmpTiles(uint baseZoom):
	TileDataSource(),
	tileIndex(baseZoom)
{

}

void OsmDiskTmpTiles::AddObject(TileCoordinates index, OutputObjectRef oo)
{
	tileIndex.Add(index, oo);
}

uint OsmDiskTmpTiles::GetBaseZoom()
{
	return tileIndex.GetBaseZoom();
}

bool OsmDiskTmpTiles::GetAvailableTileExtent(Box &clippingBox)
{
	return false;
}

// ********************************************

OsmDiskTiles::OsmDiskTiles(const std::string &basePath,
		const class Config &config,
		const std::string &luaFile,
		const class LayerDefinition &layers,	
		const class TileDataSource &shpData):
	TileDataSource(),
	basePath(basePath),
	config(config),
	luaFile(luaFile),
	layers(layers),
	shpData(shpData)
{
	//Construct decode pipeline for disk tile input
	path basePath2(basePath);
	if(is_directory(basePath2))
	{
		inTiles = make_shared<OsmDiskTilesZoomDir>(basePath);
	}
	else
	{
		
	}

	//Check what area is available
	inTiles->GetAvailable(tilesZoom,
		tileBoundsSet,
		xMin, xMax, yMin, yMax);

	//Limit available tile range if clipping box is defined. Only include tiles that are
	//in the union of these areas.
	if(config.hasClippingBox)
	{
		int xMinConf = lon2tilex(config.minLon, tilesZoom);
		int xMaxConf = lon2tilex(config.maxLon, tilesZoom)+1;
		int yMinConf = lat2tiley(config.maxLat, tilesZoom)-1;
		int yMaxConf = lat2tiley(config.minLat, tilesZoom);
 		
		if(xMinConf > xMin) xMin = xMinConf;
		if(xMaxConf < xMax) xMax = xMaxConf;
		if(yMinConf > yMin) yMin = yMinConf;
		if(yMaxConf < yMax) yMax = yMaxConf;
	}

	cout << "disk tile extent x " << xMin << "," << xMax << endl;
	cout << "y " << yMin << "," << yMax << endl;
}

void OsmDiskTiles::GenerateTileListAtZoom(uint zoom, TileCoordinatesSet &dstCoords)
{
	::GenerateTileListAtZoom(xMin, xMax, yMin, yMax, 
		tilesZoom, zoom, dstCoords);
}

void OsmDiskTiles::GetTileData(TileCoordinates dstIndex, uint zoom, 
	std::vector<OutputObjectRef> &dstTile)
{
	class LayerDefinition layersTmp(layers);
	class OsmDiskTmpTiles tmpTiles(zoom);

	OsmLuaProcessing osmLuaProcessing(config, layersTmp, luaFile, 
		shpData, 
		tmpTiles);

	if(zoom < tilesZoom)
	{
		int scale = pow(2, tilesZoom-zoom);
		TileCoordinates srcIndex1(dstIndex.x*scale, dstIndex.y*scale);
		TileCoordinates srcIndex2((dstIndex.x+1)*scale, (dstIndex.y+1)*scale);

		osmLuaProcessing.readPreprocessing = true;
		osmLuaProcessing.startOsmData();
		for(int x=srcIndex1.x; x<srcIndex2.x; x++)
		{
			if(x < xMin or x > xMax) continue;

			for(int y=srcIndex1.y; y<srcIndex2.y; y++)
			{
				if(y < yMin or y > yMax) continue;

				inTiles->GetTile(tilesZoom, x, y, &osmLuaProcessing);
			}
		}

		osmLuaProcessing.readPreprocessing = false;
		for(int x=srcIndex1.x; x<srcIndex2.x; x++)
		{
			if(x < xMin or x > xMax) continue;

			for(int y=srcIndex1.y; y<srcIndex2.y; y++)
			{
				if(y < yMin or y > yMax) continue;

				inTiles->GetTile(tilesZoom, x, y, &osmLuaProcessing);
			}
		}

	}
	else
	{
		//Convert request tile coordinates into the source tile used for input
		TileCoordinate tilex = dstIndex.x;
		TileCoordinate tiley = dstIndex.y;
		if(zoom > tilesZoom)
		{
			int scale = pow(2, zoom-tilesZoom);
			tilex = dstIndex.x / scale;
			tiley = dstIndex.y / scale;
		}

		if(tilex < xMin or tilex > xMax or tiley < yMin or tiley > yMax) return;

		// ----	Read tile file
	
		osmLuaProcessing.readPreprocessing = true;
		osmLuaProcessing.startOsmData();
		inTiles->GetTile(tilesZoom, tilex, tiley, &osmLuaProcessing);

		osmLuaProcessing.readPreprocessing = false;
		inTiles->GetTile(tilesZoom, tilex, tiley, &osmLuaProcessing);
	}

	tmpTiles.tileIndex.GetTileData(dstIndex, zoom, dstTile);

}

void OsmDiskTiles::AddObject(TileCoordinates index, OutputObjectRef oo)
{
	//This is called when loading pbfs during initialization. OsmDiskTiles don't need that
	//info, so do nothing.
}

uint OsmDiskTiles::GetBaseZoom()
{
	//This value should be unused
	return tilesZoom;
}

bool OsmDiskTiles::GetAvailableTileExtent(Box &clippingBox)
{
	uint tilesZoom = 0;
	bool tileBoundsSet = false;
	int xMin=0, xMax=0, yMin=0, yMax=0;

	inTiles->GetAvailable(tilesZoom,
		tileBoundsSet,
		xMin, xMax, yMin, yMax);

	cout << tilesZoom << endl;
	cout << "disk tile extent x " << xMin << "," << xMax << endl;
	cout << "y " << yMin << "," << yMax << endl;

	if(tileBoundsSet)
		clippingBox = Box(geom::make<Point>(tilex2lon(xMin, tilesZoom), tiley2lat(yMax+1, tilesZoom)),
		              geom::make<Point>(tilex2lon(xMax+1, tilesZoom), tiley2lat(yMin, tilesZoom)));

	cout << "QQ extent " << clippingBox.min_corner().get<0>() <<","<< clippingBox.min_corner().get<1>() \
		<<","<< clippingBox.max_corner().get<0>() <<","<< clippingBox.max_corner().get<1>() << endl;

	return tileBoundsSet;
}

