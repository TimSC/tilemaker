/*! \file */ 
#ifndef _TILE_DATA_H
#define _TILE_DATA_H

#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include <memory>
#include "coordinates_geom.h"
#include "output_object.h"

typedef std::vector<class TileDataSource *> SourceList;

class TileBbox;

class TileDataSource {
public:
	// Store for generated geometries
	using point_store_t = std::vector<Point>;

	using linestring_t = boost::geometry::model::linestring<Point>;
	using linestring_store_t = std::vector<linestring_t>;

	using multi_linestring_t = boost::geometry::model::multi_linestring<linestring_t>;
	using multi_linestring_store_t = std::vector<multi_linestring_t>;

	using polygon_t = boost::geometry::model::polygon<Point, true, true>;
	using multi_polygon_t = boost::geometry::model::multi_polygon<polygon_t>;
	using multi_polygon_store_t = std::vector<multi_polygon_t>;

	virtual std::string name() const = 0;

protected:
	bool includeID;

	// Output objects are indexed by the tiles they are in at indexZoom (z14 at
	// most), as tilemaker always has: points by their tile, lines by the tiles
	// they cross, polygons by those they cover. Polygons covering many tiles are
	// kept by the range of tiles they cover instead.
	unsigned int indexZoom;
	std::unordered_map<TileCoordinates, std::vector<OutputObjectID>> smallIndex;
	struct LargeObject {
		Box tiles; // at indexZoom
		OutputObjectID object;
	};
	std::vector<LargeObject> largeIndex;

	// The geometries of output objects, which their objectIDs index
	point_store_t pointStore;
	linestring_store_t linestringStore;
	multi_linestring_store_t multilinestringStore;
	multi_polygon_store_t multipolygonStore;

public:
	TileDataSource(unsigned int indexZoom, bool includeID);

	void addGeometryToIndex(
		const Linestring& geom,
		const std::vector<OutputObject>& outputs,
		const uint64_t id
	);
	void addGeometryToIndex(
		const MultiLinestring& geom,
		const std::vector<OutputObject>& outputs,
		const uint64_t id
	);
	void addGeometryToIndex(
		const MultiPolygon& geom,
		std::vector<OutputObject>& outputs,
		const uint64_t id
	);

	void addObjectToSmallIndex(const TileCoordinates& index, const OutputObject& oo, uint64_t id);
	void addObjectToLargeIndex(const Box& tiles, const OutputObject& oo, uint64_t id);

	// The objects in a tile, sorted for writing (see sortOutputObjectIDs)
	std::vector<OutputObjectID> getObjectsForTile(
		const std::vector<bool>& sortOrders, 
		unsigned int zoom,
		TileCoordinates coordinates
	) const;

	virtual Geometry buildWayGeometry(OutputGeometryType const geomType, NodeID const objectID, const TileBbox &bbox);
	virtual LatpLon buildNodeGeometry(NodeID const objectID, const TileBbox &bbox) const;

	void open() {
		// Put something at index 0 of all stores so that 0 can be used
		// as a sentinel.
		pointStore.push_back(Point(0,0));
		linestringStore.push_back(linestring_t());
		multipolygonStore.push_back(multi_polygon_t());
		multilinestringStore.push_back(multi_linestring_t());
	}
	void reportSize() const;

	NodeID storePoint(Point const &input);
	NodeID storeLinestring(const Linestring& src);
	NodeID storeMultiLinestring(const MultiLinestring& src);
	NodeID storeMultiPolygon(const MultiPolygon& src);

	virtual void populateMultiPolygon(MultiPolygon& dst, NodeID objectID);

	unsigned int getIndexZoom() const { return indexZoom; }

	const Point& retrievePoint(NodeID id) const { return retrieve(pointStore, id, "node"); }
	const linestring_t& retrieveLinestring(NodeID id) const { return retrieve(linestringStore, id, "linestring"); }
	const multi_linestring_t& retrieveMultiLinestring(NodeID id) const { return retrieve(multilinestringStore, id, "multi-linestring"); }
	const multi_polygon_t& retrieveMultiPolygon(NodeID id) const { return retrieve(multipolygonStore, id, "multi-polygon"); }

private:
	template<class Store>
	static const typename Store::value_type& retrieve(const Store& store, NodeID id, const char* kind) {
		if (id >= store.size()) throw std::out_of_range("Could not find generated " + std::string(kind) + " with id " + std::to_string(id));
		return store[id];
	}
};

#endif //_TILE_DATA_H
