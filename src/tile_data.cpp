#include <algorithm>
#include <iostream>
#include <sstream>
#include <boost/sort/sort.hpp>
#include "tile_data.h"
#include "coordinates_geom.h"
#include <ciso646>

using namespace std;
extern bool verbose;

// Human-readable name for a Boost.Geometry validity failure, used for diagnostics.
static const char* validityFailureName(geom::validity_failure_type failure) {
	switch (failure) {
		case geom::no_failure:                        return "no_failure";
		case geom::failure_few_points:                return "few_points";
		case geom::failure_wrong_topological_dimension: return "wrong_topological_dimension";
		case geom::failure_spikes:                    return "spikes";
		case geom::failure_duplicate_points:          return "duplicate_points";
		case geom::failure_not_closed:                return "not_closed";
		case geom::failure_self_intersections:        return "self_intersections";
		case geom::failure_wrong_orientation:         return "wrong_orientation";
		case geom::failure_interior_rings_outside:    return "interior_rings_outside";
		case geom::failure_nested_interior_rings:     return "nested_interior_rings";
		case geom::failure_disconnected_interior:     return "disconnected_interior";
		case geom::failure_intersecting_interiors:    return "intersecting_interiors";
		case geom::failure_wrong_corner_order:        return "wrong_corner_order";
		case geom::failure_invalid_coordinate:        return "invalid_coordinate";
		default:                                      return "unknown";
	}
}

// Thin wrapper around the shared repair_multi_polygon() that adds per-object
// verbose diagnostics. See geom.cpp for the dissolve + zero-width buffer logic.
static bool repairMultiPolygon(MultiPolygon &mp, NodeID objectID) {
	bool ok = repair_multi_polygon(mp);
	if (!ok && verbose)
		std::cerr << ("multipolygon repair failed for object " + std::to_string(objectID) + "\n");
	return ok;
}

TileDataSource::TileDataSource(unsigned int indexZoom, bool includeID)
	: includeID(includeID), indexZoom(indexZoom)
{
	// TileDataSource can only index up to zoom 14. The caller is responsible for
	// ensuring it does not use a higher zoom.
	if (indexZoom > 14)
		throw std::out_of_range("TileDataSource: indexZoom cannot be higher than 14, but was " + std::to_string(indexZoom));
}

void TileDataSource::addObjectToSmallIndex(const TileCoordinates& index, const OutputObject& oo, uint64_t id) {
	if (index.x >= (1u << indexZoom) || index.y >= (1u << indexZoom)) {
		if (verbose) std::cerr << "ignoring OutputObject with invalid z" << indexZoom << " coordinates " << index.x << ", " << index.y << " (id: " << id << ")" << std::endl;
		return;
	}
	smallIndex[index].push_back({oo, includeID ? id : 0});
}

void TileDataSource::addObjectToLargeIndex(const Box& tiles, const OutputObject& oo, uint64_t id) {
	largeIndex.push_back({tiles, {oo, includeID ? id : 0}});
}

// Build node and way geometries
Geometry TileDataSource::buildWayGeometry(OutputGeometryType const geomType, 
                                          NodeID const objectID, const TileBbox &bbox) {
	switch(geomType) {
		case POINT_: {
			throw std::runtime_error("unexpected geomType in buildWayGeometry");
		}

		case LINESTRING_: {
			auto const &ls = retrieveLinestring(objectID);

			MultiLinestring out;
			if(ls.empty())
				return out;

			Box extBox = bbox.getExtendBox();
			const double minX = extBox.min_corner().x(), maxX = extBox.max_corner().x();
			const double minY = extBox.min_corner().y(), maxY = extBox.max_corner().y();
			auto pointInsideExtBox = [minX, maxX, minY, maxY](const Point& p) {
				return p.x() >= minX && p.x() <= maxX && p.y() >= minY && p.y() <= maxY;
			};
			bool needsIntersection = !pointInsideExtBox(ls[0]);

			Linestring current_ls;
			geom::append(current_ls, ls[0]);

			for(size_t i = 1; i < ls.size(); ++i) {
				boost::geometry::model::segment<Point> segment(ls[i-1], ls[i]);
				if(!geom::intersects(segment, bbox.clippingBox)) {
					if(current_ls.size() > 1)
						out.push_back(std::move(current_ls));
					current_ls.clear();
				}
				geom::append(current_ls, ls[i]);
				if (!needsIntersection)
					needsIntersection = !pointInsideExtBox(ls[i]);
			}

			if(current_ls.size() > 1)
				out.push_back(std::move(current_ls));

			if (!needsIntersection)
				return out;

			MultiLinestring result;
			geom::intersection(out, extBox, result);
			return result;
		}

		case MULTILINESTRING_: {
			MultiLinestring mls;
			boost::geometry::assign(mls, retrieveMultiLinestring(objectID));

			// investigate whether filtering the constituent linestrings improves performance
			MultiLinestring result;
			geom::intersection(mls, bbox.getExtendBox(), result);
			return result;
		}

		case POLYGON_: {
			MultiPolygon input;
			populateMultiPolygon(input, objectID);

			Box box = bbox.clippingBox;
			
			if (bbox.endZoom) {
				for(auto const &p: input) {
					for(auto const &inner: p.inners()) {
						for(std::size_t i = 0; i < inner.size() - 1; ++i) 
						{
							Point p1 = inner[i];
							Point p2 = inner[i + 1];

							if(geom::within(p1, bbox.clippingBox) != geom::within(p2, bbox.clippingBox)) {
								box.min_corner() = Point(	
									std::min(box.min_corner().x(), std::min(p1.x(), p2.x())), 
									std::min(box.min_corner().y(), std::min(p1.y(), p2.y())));
								box.max_corner() = Point(	
									std::max(box.max_corner().x(), std::max(p1.x(), p2.x())), 
									std::max(box.max_corner().y(), std::max(p1.y(), p2.y())));
							}
						}
					}

					for(std::size_t i = 0; i < p.outer().size() - 1; ++i) {
						Point p1 = p.outer()[i];
						Point p2 = p.outer()[i + 1];

						if(geom::within(p1, bbox.clippingBox) != geom::within(p2, bbox.clippingBox)) {
							box.min_corner() = Point(	
								std::min(box.min_corner().x(), std::min(p1.x(), p2.x())), 
								std::min(box.min_corner().y(), std::min(p1.y(), p2.y())));
							box.max_corner() = Point(	
								std::max(box.max_corner().x(), std::max(p1.x(), p2.x())), 
								std::max(box.max_corner().y(), std::max(p1.y(), p2.y())));
						}
					}
				}

				Box extBox = bbox.getExtendBox();
				box.min_corner() = Point(	
					std::max(box.min_corner().x(), extBox.min_corner().x()), 
					std::max(box.min_corner().y(), extBox.min_corner().y()));
				box.max_corner() = Point(	
					std::min(box.max_corner().x(), extBox.max_corner().x()), 
					std::min(box.max_corner().y(), extBox.max_corner().y()));
			}

			MultiPolygon mp = input;
			fast_clip(mp, box);
			geom::correct(mp);
			geom::validity_failure_type failure = geom::validity_failure_type::no_failure;
			bool valid = geom::is_valid(mp,failure);
			if (!valid) {
				if (verbose) {
					std::ostringstream msg;
					msg << "invalid multipolygon for object " << objectID
					    << " at z" << bbox.zoom << " " << bbox.index.x << "/" << bbox.index.y
					    << ": " << validityFailureName(failure) << "\n";
					std::cerr << msg.str();
				}
				if (failure==geom::failure_spikes) {
					geom::remove_spikes(mp);
					failure = geom::validity_failure_type::no_failure;
					valid = geom::is_valid(mp,failure);
				}
				if (!valid && (failure==geom::failure_self_intersections || failure==geom::failure_intersecting_interiors)) {
					// fast_clip can introduce self-intersections; redo the clip with the
					// slower but robust Boost intersection against the original geometry.
					MultiPolygon output;
					geom::intersection(input, box, output);
					geom::correct(output);

					// The intersection result can itself still be invalid for very complex
					// multipolygons (e.g. large reservoirs), which previously produced
					// dropped or holey tiles. Repair it before returning.
					repairMultiPolygon(output, objectID);
					return output;
				} else if (!valid) {
					// occasionally also wrong_topological_dimension, disconnected_interior:
					// defects geom::correct cannot mend. Repair mp in place; on failure it
					// is left unchanged so behaviour never regresses.
					repairMultiPolygon(mp, objectID);
				}
			}

			return mp;
		}

		default:
			throw std::runtime_error("Invalid output geometry");
	}
}

LatpLon TileDataSource::buildNodeGeometry(NodeID const objectID, const TileBbox &bbox) const {
	auto p = retrievePoint(objectID);
	LatpLon out;
	out.latp = p.y();
	out.lon  = p.x();
	return out;
}


// Report number of stored geometries
void TileDataSource::reportSize() const {
	std::cout << "Generated points: " << (pointStore.size() - 1)
	          << ", lines: " << (linestringStore.size() + multilinestringStore.size() - 2)
	          << ", polygons: " << (multipolygonStore.size() - 1) << std::endl;
}

// Lexicographic comparison, with the order of: layer, geomType, attributes, and objectID.
// Note that attributes is preferred to objectID.
// It is to arrange objects with the identical attributes continuously.
// Such objects will be merged into one object, to reduce the size of output.
static void sortOutputObjectIDs(
	const std::vector<bool>& sortOrders, 
	std::vector<OutputObjectID>& data
) {
	boost::sort::pdqsort(data.begin(), data.end(), [&sortOrders](const OutputObjectID& x, const OutputObjectID& y) -> bool {
		if (x.oo.layer < y.oo.layer) return true;
		if (x.oo.layer > y.oo.layer) return false;
		if (x.oo.z_order < y.oo.z_order) return  sortOrders[x.oo.layer];
		if (x.oo.z_order > y.oo.z_order) return !sortOrders[x.oo.layer];
		if (x.oo.geomType < y.oo.geomType) return true;
		if (x.oo.geomType > y.oo.geomType) return false;
		if (x.oo.attributes < y.oo.attributes) return true;
		if (x.oo.attributes > y.oo.attributes) return false;
		if (x.oo.objectID < y.oo.objectID) return true;
		return false;
	});
}

std::vector<OutputObjectID> TileDataSource::getObjectsForTile(
	const std::vector<bool>& sortOrders, 
	unsigned int zoom,
	TileCoordinates coordinates
) const {
	// The tile's range of tiles at indexZoom; above it, the one tile it is in
	TileCoordinate minX, minY, maxX, maxY;
	if (zoom >= indexZoom) {
		minX = maxX = coordinates.x >> (zoom - indexZoom);
		minY = maxY = coordinates.y >> (zoom - indexZoom);
	} else {
		const TileCoordinate scale = 1u << (indexZoom - zoom);
		minX = coordinates.x * scale; maxX = minX + scale - 1;
		minY = coordinates.y * scale; maxY = minY + scale - 1;
	}

	std::vector<OutputObjectID> data;
	auto take = [&](const std::vector<OutputObjectID>& objects) {
		for (const auto& object : objects)
			if (object.oo.minZoom <= zoom) data.push_back(object);
	};
	// Look up each tile in the range, or go through the index if that is quicker
	const uint64_t rangeSize = uint64_t(maxX - minX + 1) * (maxY - minY + 1);
	if (rangeSize <= smallIndex.size()) {
		for (TileCoordinate x = minX; x <= maxX; x++)
			for (TileCoordinate y = minY; y <= maxY; y++) {
				auto it = smallIndex.find(TileCoordinates(x, y));
				if (it != smallIndex.end()) take(it->second);
			}
	} else {
		for (const auto& entry : smallIndex)
			if (entry.first.x >= minX && entry.first.x <= maxX && entry.first.y >= minY && entry.first.y <= maxY)
				take(entry.second);
	}

	Box range(geom::make<Point>(minX, minY), geom::make<Point>(maxX, maxY));
	for (const auto& large : largeIndex)
		if (large.object.oo.minZoom <= zoom && geom::intersects(large.tiles, range))
			data.push_back(large.object);

	// An object in several of the tiles is found more than once
	sortOutputObjectIDs(sortOrders, data);
	data.erase(unique(data.begin(), data.end()), data.end());
	return data;
}

// ------------------------------------
// Add geometries to tile/large indices

void TileDataSource::addGeometryToIndex(
	const Linestring& geom,
	const std::vector<OutputObject>& outputs,
	const uint64_t id
) {
	unordered_set<TileCoordinates> tileSet;
	try {
		insertIntermediateTiles(geom, indexZoom, tileSet);

		bool polygonExists = false;
		TileCoordinate minTileX = std::numeric_limits<TileCoordinate>::max(), maxTileX = 0, minTileY = std::numeric_limits<TileCoordinate>::max(), maxTileY = 0;
		for (auto it = tileSet.begin(); it != tileSet.end(); ++it) {
			TileCoordinates index = *it;
			minTileX = std::min(index.x, minTileX);
			minTileY = std::min(index.y, minTileY);
			maxTileX = std::max(index.x, maxTileX);
			maxTileY = std::max(index.y, maxTileY);
			for (const auto& output : outputs) {
				if (output.geomType == POLYGON_) {
					polygonExists = true;
					continue;
				}
				addObjectToSmallIndex(index, output, id); // not a polygon
			}
		}

		// for polygon, fill inner tiles
		if (polygonExists) {
			bool tilesetFilled = false;
			uint size = (maxTileX - minTileX + 1) * (maxTileY - minTileY + 1);
			for (const auto& output : outputs) {
				if (output.geomType != POLYGON_) continue;
				if (size>= 16) {
					// Larger objects - add to rtree
					Box box = Box(geom::make<Point>(minTileX, minTileY),
					              geom::make<Point>(maxTileX, maxTileY));
					addObjectToLargeIndex(box, output, id);
				} else {
					// Smaller objects - add to each individual tile index
					if (!tilesetFilled) { fillCoveredTiles(tileSet); tilesetFilled = true; }
					for (auto it = tileSet.begin(); it != tileSet.end(); ++it) {
						TileCoordinates index = *it;
						addObjectToSmallIndex(index, output, id);
					}
				}
			}
		}
	} catch(std::out_of_range &err) {
		cerr << "Error calculating intermediate tiles: " << err.what() << endl;
	}
}

void TileDataSource::addGeometryToIndex(
	const MultiLinestring& geom,
	const std::vector<OutputObject>& outputs,
	const uint64_t id
) {
	for (const auto& ls : geom) {
		unordered_set<TileCoordinates> tileSet;
		insertIntermediateTiles(ls, indexZoom, tileSet);
		for (auto it = tileSet.begin(); it != tileSet.end(); ++it) {
			TileCoordinates index = *it;
			for (const auto& output : outputs) {
				addObjectToSmallIndex(index, output, id);
			}
		}
	}
}

void TileDataSource::addGeometryToIndex(
	const MultiPolygon& geom,
	std::vector<OutputObject>& outputs,
	const uint64_t id
) {
	unordered_set<TileCoordinates> tileSet;
	bool singleOuter = geom.size()==1;
	for (const auto& poly : geom) {
		unordered_set<TileCoordinates> tileSetTmp;
		insertIntermediateTiles(poly.outer(), indexZoom, tileSetTmp);
		fillCoveredTiles(tileSetTmp);
		if (singleOuter) {
			tileSet = std::move(tileSetTmp);
		} else {
			tileSet.insert(tileSetTmp.begin(), tileSetTmp.end());
		}
	}
	
	TileCoordinate minTileX = std::numeric_limits<TileCoordinate>::max(), maxTileX = 0, minTileY = std::numeric_limits<TileCoordinate>::max(), maxTileY = 0;
	for (auto it = tileSet.begin(); it != tileSet.end(); ++it) {
		TileCoordinates index = *it;
		minTileX = std::min(index.x, minTileX);
		minTileY = std::min(index.y, minTileY);
		maxTileX = std::max(index.x, maxTileX);
		maxTileY = std::max(index.y, maxTileY);
	}

	const size_t tileSetSize = tileSet.size();
	for (auto& output : outputs) {
		if (tileSetSize >= 16) {
			// Larger objects - add to rtree
			// note that the bbox is currently the envelope of the entire multipolygon,
			// which is suboptimal in shapes like (_) ...... (_) where the outers are significantly disjoint
			Box box = Box(geom::make<Point>(minTileX, minTileY),
			              geom::make<Point>(maxTileX, maxTileY));
			addObjectToLargeIndex(box, output, id);
		} else {
			// Smaller objects - add to each individual tile index
			for (auto it = tileSet.begin(); it != tileSet.end(); ++it) {
				TileCoordinates index = *it;
				addObjectToSmallIndex(index, output, id);
			}
		}
	}
}

NodeID TileDataSource::storePoint(const Point& input) {
	pointStore.emplace_back(input);
	return pointStore.size() - 1;
}

NodeID TileDataSource::storeLinestring(const Linestring& src) {
	linestringStore.emplace_back(src.begin(), src.end());
	return linestringStore.size() - 1;
}

NodeID TileDataSource::storeMultiPolygon(const MultiPolygon& src) {
	multi_polygon_t dst;
	dst.resize(src.size());
	for(std::size_t i = 0; i < src.size(); ++i) {
		dst[i].outer().resize(src[i].outer().size());
		boost::geometry::assign(dst[i].outer(), src[i].outer());

		dst[i].inners().resize(src[i].inners().size());
		for(std::size_t j = 0; j < src[i].inners().size(); ++j) {
			dst[i].inners()[j].resize(src[i].inners()[j].size());
			boost::geometry::assign(dst[i].inners()[j], src[i].inners()[j]);
		}
	}
	multipolygonStore.emplace_back(std::move(dst));
	return multipolygonStore.size() - 1;
}

NodeID TileDataSource::storeMultiLinestring(const MultiLinestring& src) {
	multi_linestring_t dst;
	dst.resize(src.size());
	for (std::size_t i=0; i<src.size(); ++i) {
		boost::geometry::assign(dst[i], src[i]);
	}
	multilinestringStore.emplace_back(std::move(dst));
	return multilinestringStore.size() - 1;
}

void TileDataSource::populateMultiPolygon(MultiPolygon& dst, NodeID objectID) {
	const auto &input = retrieveMultiPolygon(objectID);
	dst.resize(input.size());
	for(std::size_t i = 0; i < input.size(); ++i) {
		dst[i].outer().resize(input[i].outer().size());
		boost::geometry::assign(dst[i].outer(), input[i].outer());

		dst[i].inners().resize(input[i].inners().size());
		for(std::size_t j = 0; j < input[i].inners().size(); ++j) {
			dst[i].inners()[j].resize(input[i].inners()[j].size());
			boost::geometry::assign(dst[i].inners()[j], input[i].inners()[j]);
		}
	}
}
