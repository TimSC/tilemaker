#include "osm_mem_tiles.h"
#include <fstream>
#include "osm_lua_processing.h"
#include "../cppo5m/utils.h"
using namespace std;

OsmMemTiles::OsmMemTiles(uint baseZoom,
		const vector<string> &inputFiles,
		const class Config &config,
		const std::string &luaFile,
		const class LayerDefinition &layers,	
		const class TileDataSource &shpMemTiles):

	TileDataSource(),
	tileIndex(baseZoom)
{
	class LayerDefinition layersTmp(layers);
	std::shared_ptr<OsmLuaProcessing> osmLuaProcessing = make_shared<OsmLuaProcessing>(config, layersTmp, luaFile, 
		shpMemTiles, 
		*this);

	// ----	Read all PBFs

	for (auto inputFile : inputFiles) {

		cout << "Reading " << inputFile << endl;

		osmLuaProcessing->readPreprocessing = true;
		osmLuaProcessing->startOsmData();
		std::filebuf infi;
		infi.open(inputFile, std::ios::in);
		std::shared_ptr<class OsmDecoder> decoder = DecoderOsmFactory(infi, inputFile);
		LoadFromDecoder(infi, decoder.get(), osmLuaProcessing.get());

		osmLuaProcessing->readPreprocessing = false;
		infi.pubseekpos(0);
		decoder = DecoderOsmFactory(infi, inputFile);
		LoadFromDecoder(infi, decoder.get(), osmLuaProcessing.get());
	}
}

void OsmMemTiles::GenerateTileListAtZoom(uint zoom, TileCoordinatesSet &dstCoords)
{
	tileIndex.GenerateTileList(zoom, dstCoords);
}

void OsmMemTiles::GetTileData(TileCoordinates dstIndex, uint zoom, 
	std::vector<OutputObjectRef> &dstTile)
{
	tileIndex.GetTileData(dstIndex, zoom, dstTile);
}

void OsmMemTiles::AddObject(TileCoordinates index, OutputObjectRef oo)
{
	tileIndex.Add(index, oo);
}

uint OsmMemTiles::GetBaseZoom()
{
	return tileIndex.GetBaseZoom();
}

