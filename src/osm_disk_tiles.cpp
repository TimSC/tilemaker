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

vector<string> PathToStringVector(path &p)
{
	vector<string> out;
	for(auto it=p.begin(); it!=p.end(); it++)
		out.push_back(it->string());
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
	infi.open(inputFile.string(), std::ios::in | std::ios::binary);
	LoadFromPbf(infi, output);
}

// *****************************************

OsmDiskTilesZoomTar::OsmDiskTilesZoomTar(std::string pth) : OsmDiskTilesZoom()
{
	tarPath = pth;
	tilesZoom = 0;
	tileBoundsSet = false;
	xMin = 0; xMax = 0; yMin = 0; yMax = 0;

	// Check for available zoom folders, choose the highest zoom
	this->infi.open(tarPath, std::ios::in | std::ios::binary);
	if(!this->infi.is_open())
		throw runtime_error("Error opening tile tar");
	this->seekableTarRead = make_shared<class SeekableTarRead>(infi);
	seekableTarRead->BuildIndex();
	std::vector<tar_header> &fileList = seekableTarRead->fileList;

	// Determine zoom of available tile files
	int maxZoom = -1;
	vector<int> zoomList;
	vector<string> filenameList;
	for(size_t i=0; i<fileList.size(); i++)
	{
		path p (fileList[i].name);
		vector<string> psv = PathToStringVector(p);
		if(psv.size() != 2) continue;
		int z = atoi(psv[0].c_str());
		if(maxZoom < z)
			maxZoom = z;
		zoomList.push_back(maxZoom);
		filenameList.push_back(psv[1]);
	}

	if(maxZoom < 0)
		throw runtime_error("Zoom of tar could not be determined");
	this->tilesZoom = maxZoom;

	// Determine extent of available tile files
	for(size_t i=0; i<zoomList.size(); i++)
	{
		if(zoomList[i] != this->tilesZoom) 
			continue;
		size_t dot = filenameList[i].find(".");
		if(dot == string::npos) continue;
		string ext = filenameList[i].substr(dot);
		if(ext!=".tar.gz") continue;

		string basename = filenameList[i].substr(0, dot);
		int tilex = atoi(basename.c_str());
		if(!tileBoundsSet)
		{
			this->xMin = tilex;
			this->xMax = tilex;
			tileBoundsSet = true;
		}
		else
		{
			if(tilex < xMin) this->xMin = tilex;
			if(tilex > xMax) this->xMax = tilex;
		}

		this->tarEntries[tilex] = seekableTarRead->GetEntry(i);
	}

	if(this->tarEntries.size() == 0 || !tileBoundsSet)
		throw runtime_error("Extent of tar tiles could not be determined in x direction");

	//Check tile extent in column
	std::shared_ptr<class SeekableTarEntry> firstTile = this->tarEntries[0];
	class DecodeGzip dec(*firstTile);
	class SeekableTarRead colReader(dec);
	colReader.BuildIndex();

	this->tileBoundsSet = false;
	std::vector<tar_header> &colFileList = colReader.fileList;
	for(size_t i=0; i<colFileList.size(); i++)
	{
		string fina = colFileList[i].name;
		size_t dot = fina.find(".");
		if(dot == string::npos) continue;
		string ext = fina.substr(dot);
		if(ext != ".o5m") continue;
		string basename = fina.substr(0, dot);

		int tiley = atoi(basename.c_str());
		if(!this->tileBoundsSet)
		{
			this->yMin = tiley;
			this->yMax = tiley;
			this->tileBoundsSet = true;
		}
		else
		{
			if(tiley < yMin) this->yMin = tiley;
			if(tiley > yMax) this->yMax = tiley;
		}

	}
	if(colFileList.size() == 0 || !tileBoundsSet)
		throw runtime_error("Extent of tar tiles could not be determined in y direction");
}

OsmDiskTilesZoomTar::~OsmDiskTilesZoomTar()
{

}

void OsmDiskTilesZoomTar::GetAvailable(uint &tilesZoom,
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

void OsmDiskTilesZoomTar::GetTile(uint zoom, int x, int y, class IDataStreamHandler *output)
{
	if(zoom != this->tilesZoom)
		throw runtime_error("Tiles can only be accessed at the zoom returned by GetAvailable");

	auto xit = tarEntries.find(x);
	if(xit == tarEntries.end())
		return; //Out of range in x

	m.lock();

	auto colit = colTarDec.find(x);
	if(colit == colTarDec.end())
	{
		//Need to decode this column tar
		DecodeGzipIndex index;
		CreateDecodeGzipIndex(*tarEntries[x].get(), index);

		colTarDec[x] = make_shared<class DecodeGzipFastSeek>(*tarEntries[x].get(), index);
		colit = colTarDec.find(x);
	}

	auto colit2 = colTarReaders.find(x);
	if(colit2 == colTarReaders.end())
	{
		//Need to parse this column tar
		colTarReaders[x] = make_shared<class SeekableTarRead>(*colit->second.get());
		colTarReaders[x]->BuildIndex();
		colit2 = colTarReaders.find(x);
	}
	
	//Search for matching tile in y
	std::vector<tar_header> &colFileList = colit2->second->fileList;
	int foundEntry = -1;
	for(size_t i=0; i<colFileList.size(); i++)
	{
		string fina = colFileList[i].name;
		size_t dot = fina.find(".");
		if(dot == string::npos) continue;
		string ext = fina.substr(dot);
		if(ext != ".o5m") continue;
		string basename = fina.substr(0, dot);

		int tiley = atoi(basename.c_str());
		if(tiley == y)
		{
			foundEntry = i;
			break;
		}
	}
	
	//Extract the relevant data
	std::stringbuf buff;
	if(foundEntry != -1)
		colit2->second->ExtractByIndex(foundEntry, buff);
	m.unlock();

	if(foundEntry == -1) return;

	//Decode the data in our own thread
	buff.pubseekpos(0);
	LoadFromO5m(buff, output);
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

bool OsmDiskTmpTiles::GetAvailableTileExtent(Box &clippingBox, std::string &boxSource)
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
		path p (basePath);
		if(p.extension().string() == ".tar")
			inTiles = make_shared<OsmDiskTilesZoomTar>(basePath);
	}

	if(!inTiles)
		throw runtime_error("Input tile format not recognized");

	uint tilesZoom = 0;
	bool tileBoundsSet = false;
	int xMin=0, xMax=0, yMin=0, yMax=0;

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
	uint tilesZoom = 0;
	bool tileBoundsSet = false;
	int xMin=0, xMax=0, yMin=0, yMax=0;

	inTiles->GetAvailable(tilesZoom,
		tileBoundsSet,
		xMin, xMax, yMin, yMax);

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

	uint tilesZoom = 0;
	bool tileBoundsSet = false;
	int xMin=0, xMax=0, yMin=0, yMax=0;

	inTiles->GetAvailable(tilesZoom,
		tileBoundsSet,
		xMin, xMax, yMin, yMax);

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
	uint tilesZoom = 0;
	bool tileBoundsSet = false;
	int xMin=0, xMax=0, yMin=0, yMax=0;

	inTiles->GetAvailable(tilesZoom,
		tileBoundsSet,
		xMin, xMax, yMin, yMax);

	//This value should be unused
	return tilesZoom;
}

bool OsmDiskTiles::GetAvailableTileExtent(Box &clippingBox, std::string &boxSource)
{
	uint tilesZoom = 0;
	bool tileBoundsSet = false;
	int xMin=0, xMax=0, yMin=0, yMax=0;

	inTiles->GetAvailable(tilesZoom,
		tileBoundsSet,
		xMin, xMax, yMin, yMax);

	if(tileBoundsSet)
	{
		clippingBox = Box(geom::make<Point>(tilex2lon(xMin, tilesZoom), tiley2lat(yMax+1, tilesZoom)),
		              geom::make<Point>(tilex2lon(xMax+1, tilesZoom), tiley2lat(yMin, tilesZoom)));
		boxSource="tiles on disk (OsmDiskTiles)";
	}

	return tileBoundsSet;
}

