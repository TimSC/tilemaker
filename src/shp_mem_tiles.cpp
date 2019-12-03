#include "shp_mem_tiles.h"
#include <iostream>
using namespace std;
namespace geom = boost::geometry;
#include "read_shp.h"

ShpMemTiles::ShpMemTiles(uint baseZoom):
	TileDataSource(),
	tileIndex(baseZoom)
{

}


void ShpMemTiles::GetTileData(TileCoordinates dstIndex, uint zoom, 
	std::vector<OutputObjectRef> &dstTile)
{
	tileIndex.GetTileData(dstIndex, zoom, dstTile);
}

// Find intersecting shapefile layer
// TODO: multipolygon relations not supported, will always return false
vector<string> ShpMemTiles::FindIntersecting(const string &layerName, Box &box) const {
	vector<uint> ids = tileIndex.findIntersectingGeometries(layerName, box);
	return tileIndex.namesOfGeometries(ids);
}

bool ShpMemTiles::Intersects(const string &layerName, Box &box) const {
	return !tileIndex.findIntersectingGeometries(layerName, box).empty();
}

uint ShpMemTiles::GetBaseZoom()
{
	return tileIndex.GetBaseZoom();
}

void ShpMemTiles::Load(class LayerDefinition &layers, 
	bool hasClippingBox,
	const Box &clippingBox)
{
	ShapeFileToLayers layerConverter(layers);
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

			const string &filename = layer.source;
			const vector<string> &columns = layer.sourceColumns;
			layerConverter.layerNum = layerNum;

			prepareShapefile(filename, columns, layerConverter);
		}
	}

	ShapeFileToTileIndexCached converter(this->tileIndex, layers);
	Box projClippingBox = Box(geom::make<Point>(clippingBox.min_corner().get<0>(), lat2latp(clippingBox.min_corner().get<1>())),
	              geom::make<Point>(clippingBox.max_corner().get<0>(), lat2latp(clippingBox.max_corner().get<1>())));
	for(size_t layerNum=0; layerNum<layers.layers.size(); layerNum++)	
	{
		// External layer sources
		const LayerDef &layer = layers.layers[layerNum];

		if (layer.source.size()>0) {
			converter.layerNum = layerNum;
			const string &filename = layer.source;
			const vector<string> &columns = layer.sourceColumns;
			const string &indexName = layer.indexName;

			class ShapefileReader shapefileReader(filename,
						  columns,
			              indexName);
			shapefileReader.ReadAllInBox(projClippingBox, converter);
		}
	}
}

