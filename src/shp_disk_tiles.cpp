#include "shp_disk_tiles.h"
#include <iostream>
using namespace std;
namespace geom = boost::geometry;
#include "read_shp.h"

ShpDiskTiles::ShpDiskTiles(uint baseZoom, const class LayerDefinition &layers):
	TileDataSource(),
	layers(layers),
	tileIndex(baseZoom)
{

}

void ShpDiskTiles::GenerateTileListAtZoom(uint zoom, TileCoordinatesSet &dstCoords)
{
	tileIndex.GenerateTileList(zoom, dstCoords);
}

void ShpDiskTiles::GetTileData(TileCoordinates dstIndex, uint zoom, 
	std::vector<OutputObjectRef> &dstTile)
{
	class TileIndexCached tmpTileIndex(zoom);

	Box clippingBox = Box(geom::make<Point>(tilex2lon(dstIndex.x, zoom), tiley2lat(dstIndex.y+1, zoom)),
	              geom::make<Point>(tilex2lon(dstIndex.x+1, zoom), tiley2lat(dstIndex.y, zoom)));

	Box projClippingBox = Box(geom::make<Point>(clippingBox.min_corner().get<0>(), lat2latp(clippingBox.min_corner().get<1>())),
	              geom::make<Point>(clippingBox.max_corner().get<0>(), lat2latp(clippingBox.max_corner().get<1>())));

	for(size_t layerNum=0; layerNum<layers.layers.size(); layerNum++)	
	{
		const LayerDef &layer = layers.layers[layerNum];
		if(layer.indexed)
			this->tileIndex.CreateNamedLayerIndex(layer.name);
	}

	for(size_t layerNum=0; layerNum<layers.layers.size(); layerNum++)	
	{
		// External layer sources
		const LayerDef &layer = layers.layers[layerNum];

		if (layer.source.size()==0) continue;

		readShapefile(projClippingBox,
		              layers,
		              tmpTileIndex.GetBaseZoom(), layerNum,
					  tmpTileIndex);
		
	}

	tmpTileIndex.GetTileData(dstIndex, zoom, dstTile);
}

// Find intersecting shapefile layer
// TODO: multipolygon relations not supported, will always return false
vector<string> ShpDiskTiles::FindIntersecting(const string &layerName, Box &box) const {
	vector<uint> ids = tileIndex.findIntersectingGeometries(layerName, box);
	return tileIndex.namesOfGeometries(ids);
}

bool ShpDiskTiles::Intersects(const string &layerName, Box &box) const {
	return !tileIndex.findIntersectingGeometries(layerName, box).empty();
}

uint ShpDiskTiles::GetBaseZoom()
{
	return tileIndex.GetBaseZoom();
}

void ShpDiskTiles::Load(class LayerDefinition &layers, 
	bool hasClippingBox,
	const Box &clippingBox)
{
	for(size_t layerNum=0; layerNum<layers.layers.size(); layerNum++)	
	{
		const LayerDef &layer = layers.layers[layerNum];
		if(layer.indexed)
			this->tileIndex.CreateNamedLayerIndex(layer.name);

		if (layer.source.size()>0) {
			if (!hasClippingBox) {
				cerr << "Can't read shapefiles unless a bounding box is provided." << endl;
				exit(EXIT_FAILURE);
			}

			prepareShapefile(layers, tileIndex.GetBaseZoom(), layerNum);
		}
	}

	//TODO remove this up front loading of shapefile data
	for(size_t layerNum=0; layerNum<layers.layers.size(); layerNum++)	
	{
		// External layer sources
		const LayerDef &layer = layers.layers[layerNum];

		if (layer.source.size()>0) {
			Box projClippingBox = Box(geom::make<Point>(clippingBox.min_corner().get<0>(), lat2latp(clippingBox.min_corner().get<1>())),
			              geom::make<Point>(clippingBox.max_corner().get<0>(), lat2latp(clippingBox.max_corner().get<1>())));

			readShapefile(projClippingBox,
			              layers,
			              tileIndex.GetBaseZoom(), layerNum,
						  this->tileIndex);
		}
	}
}


