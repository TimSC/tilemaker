#include "shp_disk_tiles.h"
#include <iostream>
using namespace std;
namespace geom = boost::geometry;
namespace bgi = geom::index;
#include "read_shp.h"

ShpDiskTiles::ShpDiskTiles(uint baseZoom, const class LayerDefinition &layers):
	TileDataSource(),
	layers(layers),
	bareTileIndex(baseZoom),
	baseZoom(baseZoom)
{
	xMin = 0; xMax = 0; yMin = 0; yMax = 0;
}

ShpDiskTiles::~ShpDiskTiles()
{
	shapefileReaderThreadMap.clear();
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
			tmpTileIndex.CreateNamedLayerIndex(layer.name);
	}

	ShapeFileToTileIndexCached converter(tmpTileIndex, layers);
	class ShapeFileObject shpObject;
	for(size_t layerNum=0; layerNum<layers.layers.size(); layerNum++)	
	{
		// External layer sources
		const LayerDef &layer = layers.layers[layerNum];

		if (layer.source.size()==0) continue;

		converter.layerNum = layerNum;
		const string &filename = layer.source;
		const vector<string> &columns = layer.sourceColumns;
		const string &indexName = layer.indexName;

		//Each thread must have its own shapelib objects. Open them and keep open for later use.
		mtx.lock();
		auto shapefileReaderMap = shapefileReaderThreadMap[std::this_thread::get_id()];
		auto itr = shapefileReaderMap.find(layerNum);
		std::shared_ptr<class ShapefileReader> sfr;
		if (itr == shapefileReaderMap.end())
		{
			std::shared_ptr<class ShapefileReader> sfrn(std::make_shared<class ShapefileReader>(filename,
					  columns,
		              indexName));
			sfr = sfrn; //Apparently, shared_ptr counts are thread safe.
			shapefileReaderMap[layerNum] = sfrn;
		}
		else
			sfr = itr->second;
		mtx.unlock();

		//Find relevant shape objects from spatial index
		RTree &bi = this->bareTileIndex.indices[layer.name];
		vector<std::pair<Box, unsigned int> > result;
		bi.query(bgi::intersects(projClippingBox), std::back_inserter(result));

		for(size_t i=0; i<result.size(); i++)
		{
			//Check of shape object is cached
			unsigned int shpIndex = result[i].second;
			auto it = shapeObjectCache.find(shpIndex);
			cout << "cache hit " << (it != shapeObjectCache.end()) << endl;
			if(it != shapeObjectCache.end())
			{
				it->second.CopyTo(converter);
				continue;
			}

			//Read shape object from file
			sfr->ReadIndexInBox(shpIndex, false, projClippingBox, shpObject);

			shapeObjectCache[shpIndex] = shpObject;
			shpObject.CopyTo(converter);
		}
	}

	tmpTileIndex.GetTileData(dstIndex, zoom, dstTile);
}

// Find intersecting shapefile layer
// TODO: multipolygon relations not supported, will always return false
vector<string> ShpDiskTiles::FindIntersecting(const string &layerName, Box &box) const 
{
	vector<IndexValue> candidates = this->bareTileIndex.findIntersectingGeometries(layerName, box);
	//No need to run verifyIntersectResults since RTree already returns final results

	vector<unsigned int> candidateIndices;
	for(size_t i=0; i<candidates.size(); i++)
		candidateIndices.push_back(candidates[i].second);

	return this->bareTileIndex.namesOfGeometries(candidateIndices);
}

bool ShpDiskTiles::Intersects(const string &layerName, Box &box) const 
{
	vector<IndexValue> candidates = this->bareTileIndex.findIntersectingGeometries(layerName, box);
	//No need to run verifyIntersectResults since RTree already returns final results

	return !candidates.empty();
}

uint ShpDiskTiles::GetBaseZoom()
{
	return baseZoom;
}

void ShpDiskTiles::Load(class LayerDefinition &layers, 
	bool hasClippingBox,
	const Box &clippingBox)
{
	this->clippingBox = clippingBox;
	this->xMin = lon2tilex(clippingBox.min_corner().get<0>(), baseZoom);
	this->xMax = lon2tilex(clippingBox.max_corner().get<0>(), baseZoom)+1;
	this->yMin = lat2tiley(clippingBox.max_corner().get<1>(), baseZoom)-1;
	this->yMax = lat2tiley(clippingBox.min_corner().get<1>(), baseZoom);

	cout << "prepareShapefile" << endl;

	ShapeFileToLayers layerConverter(layers);
	for(size_t layerNum=0; layerNum<layers.layers.size(); layerNum++)	
	{
		const LayerDef &layer = layers.layers[layerNum];
		if(layer.indexed)
			this->bareTileIndex.CreateNamedLayerIndex(layer.name);

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

	cout << "Indexing shapefile" << endl;

	//Read shapefile layer into a simple index
	ShapeFileToBareTileIndex converterBareTileIndex(this->bareTileIndex, layers);
	Box projClippingBox = Box(geom::make<Point>(clippingBox.min_corner().get<0>(), lat2latp(clippingBox.min_corner().get<1>())),
				  geom::make<Point>(clippingBox.max_corner().get<0>(), lat2latp(clippingBox.max_corner().get<1>())));
	for(size_t layerNum=0; layerNum<layers.layers.size(); layerNum++)	
	{
		// External layer sources
		const LayerDef &layer = layers.layers[layerNum];

		if (layer.source.size()>0) {
			converterBareTileIndex.layerNum = layerNum;
			const string &filename = layer.source;
			const vector<string> &columns = layer.sourceColumns;
			const string &indexName = layer.indexName;

			//Open shapefile but keep it open for later use
			std::shared_ptr<class ShapefileReader> sfr = std::make_shared<class ShapefileReader>(filename,
					  columns,
		              indexName);

			sfr->ReadAllInBox(projClippingBox, converterBareTileIndex);
		}
	}

}

bool ShpDiskTiles::GetAvailableTileExtent(Box &clippingBox, std::string &boxSource)
{
	return false;
}

