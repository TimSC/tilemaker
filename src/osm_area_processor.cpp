#include <cmath>
#include <deque>
#include <iostream>
#include <unordered_set>

#include "osm_area_processor.h"
#include "osm_lua_processing.h"
#include "osm_store.h"
#include "tag_map.h"
#include <boost/geometry/io/wkt/read.hpp>

using namespace std;

namespace {

// The largest way ID tilemaker's output object IDs can hold
const uint64_t MAX_WAY_ID = 1ull << 42;

// An object's tags as a TagMap, which views the strings
template<class Tags>
void readTags(const Tags& in, TagMap& tags) {
	tags.reset();
	for (const auto& tag : in)
		tags.addTag(
			protozero::data_view(tag.first.data(), tag.first.size()),
			protozero::data_view(tag.second.data(), tag.second.size()));
}

const string* findTag(const OsmAreaTags& tags, const string& key) {
	for (const auto& tag : tags)
		if (tag.first == key) return &tag.second;
	return nullptr;
}

bool relationIsType(const OsmAreaRelation& relation, const string& type) {
	const string* value = findTag(relation.tags, "type");
	return value != nullptr && *value == type;
}

LatpLon toLatpLon(double lat, double lon) {
	return { int(lat2latp(lat) * 10000000.0), int(std::lround(lon * 10000000.0)) };
}

// Shapes are read in degrees of latitude; tilemaker works in latp
template<class Geometry>
void projectLatp(Geometry& geometry) {
	geom::for_each_point(geometry, [](Point& p) { p.set<1>(lat2latp(p.get<1>())); });
}

// A cached way shape (WKT in degrees) as node locations
bool readWayShape(const string& wkt, LatpLonVec& llVec) {
	Linestring ls;
	try {
		geom::read_wkt(wkt, ls);
	} catch (const geom::read_wkt_exception&) {
		return false;
	}
	for (const Point& p : ls)
		llVec.push_back(toLatpLon(p.get<1>(), p.get<0>()));
	return !llVec.empty();
}

// A cached relation shape: a MULTIPOLYGON for an area, otherwise a MULTILINESTRING
bool readRelationShape(const string& wkt, RelationShape& shape) {
	try {
		shape.isArea = wkt.rfind("MULTIPOLYGON", 0) == 0;
		if (shape.isArea) {
			shape.area.clear();
			geom::read_wkt(wkt, shape.area);
			projectLatp(shape.area);
			geom::correct(shape.area);
		} else {
			shape.lines.clear();
			geom::read_wkt(wkt, shape.lines);
			projectLatp(shape.lines);
		}
	} catch (const geom::read_wkt_exception&) {
		return false;
	}
	return true;
}

// Relations: let relation_scan_function accept the ones relation_function will
// see, and note which ways are wanted by relations even if way_keys skips them.
void scanRelations(const OsmArea& area, OSMStore& osmStore, OsmLuaProcessing& output,
                   const SignificantTags& wayKeys, unordered_set<WayID>& usedWays) {
	TagMap tags;
	for (const OsmAreaRelation& relation : area.relations) {
		RelationID relid = static_cast<RelationID>(relation.id);
		readTags(relation.tags, tags);

		bool isAccepted = false;
		if (!relationIsType(relation, "multipolygon")) {
			if (output.canReadRelations())
				isAccepted = output.scanRelation(relid, tags);
			if (!isAccepted) continue;
		} else if (!wayKeys.filter(tags)) {
			continue;
		}

		osmStore.usedRelations.insert(relid);
		for (const OsmAreaMember& member : relation.members) {
			switch (member.type) {
				case OsmAreaMember::Type::Node:
					if (isAccepted)
						osmStore.scannedRelations.relation_contains_node(relid, member.id, member.role);
					break;
				case OsmAreaMember::Type::Way:
					usedWays.insert(member.id);
					if (isAccepted)
						osmStore.scannedRelations.relation_contains_way(relid, member.id, member.role);
					break;
				case OsmAreaMember::Type::Relation:
					if (isAccepted)
						osmStore.scannedRelations.relation_contains_relation(relid, member.id, member.role);
					break;
			}
		}
	}
	output.postScanRelations();
}

void processNodes(const OsmArea& area, OSMStore& osmStore, OsmLuaProcessing& output,
                  const SignificantTags& nodeKeys) {
	TagMap tags;
	for (const OsmAreaNode& node : area.nodes) {
		NodeID nodeId = static_cast<NodeID>(node.id);
		LatpLon latplon = toLatpLon(node.lat, node.lon);
		osmStore.nodes.insert(nodeId, latplon);

		if (node.tags.empty() || !output.canWriteNodes()) continue;
		readTags(node.tags, tags);
		if (nodeKeys.filter(tags))
			output.setNode(nodeId, latplon, tags);
	}
}

// The node locations of a way, which every node of should be in the area.
// Returns false if any are missing.
bool wayNodeLocations(const OsmAreaWay& way, const OSMStore& osmStore, LatpLonVec& llVec) {
	llVec.reserve(way.refs.size());
	bool complete = true;
	for (int64_t ref : way.refs) {
		try {
			llVec.push_back(osmStore.nodes.at(static_cast<NodeID>(ref)));
		} catch (out_of_range&) {
			complete = false;
		}
	}
	return complete;
}

void processWays(const OsmArea& area, OSMStore& osmStore, OsmLuaProcessing& output,
                 const SignificantTags& wayKeys, const unordered_set<WayID>& usedWays) {
	TagMap tags;
	LatpLonVec llVec;
	size_t incomplete = 0;
	for (const OsmAreaWay& way : area.ways) {
		WayID wayId = static_cast<WayID>(way.id);
		if (wayId >= MAX_WAY_ID) throw runtime_error("Way ID negative or too large: " + to_string(way.id));

		// A cached shape stands in for the nodes
		llVec.clear();
		if (way.shapeWkt.empty() || !readWayShape(way.shapeWkt, llVec)) {
			llVec.clear();
			if (!wayNodeLocations(way, osmStore, llVec)) {
				incomplete++;
				if (osmStore.integrity_enforced()) continue;
			}
		}
		if (llVec.empty()) continue;

		// Every way is kept, for relations
		osmStore.ways.insert(wayId, std::vector<LatpLon>(llVec.begin(), llVec.end()));

		if (!output.canWriteWays()) continue;
		readTags(way.tags, tags);
		if (!usedWays.count(wayId) && !wayKeys.filter(tags)) continue;
		try {
			output.setWay(wayId, llVec, tags);
		} catch (out_of_range& err) {
			cerr << "Way " << wayId << ": " << err.what() << endl;
		}
	}
	if (incomplete > 0)
		cerr << incomplete << " ways are missing nodes" << (osmStore.integrity_enforced() ? " and were skipped" : "") << endl;
}

void processRelations(const OsmArea& area, OSMStore& osmStore, OsmLuaProcessing& output,
                      const SignificantTags& wayKeys) {
	TagMap tags;
	WayVec outerWayVec, innerWayVec;
	vector<RelationNodeMember> nodeMembers;
	RelationShape shape;
	for (const OsmAreaRelation& relation : area.relations) {
		bool isMultiPolygon = relationIsType(relation, "multipolygon");
		bool isBoundary = relationIsType(relation, "boundary");
		if (!isMultiPolygon && !isBoundary && !output.canWriteRelations()) continue;

		RelationID relid = static_cast<RelationID>(relation.id);
		outerWayVec.clear();
		innerWayVec.clear();
		nodeMembers.clear();
		bool isInnerOuter = isBoundary || isMultiPolygon;
		for (const OsmAreaMember& member : relation.members) {
			if (member.type == OsmAreaMember::Type::Node) {
				nodeMembers.push_back({static_cast<NodeID>(member.id), member.role});
			} else if (member.type == OsmAreaMember::Type::Way) {
				if (member.role == "inner" || member.role == "outer") isInnerOuter = true;
				(member.role == "inner" ? innerWayVec : outerWayVec).push_back(static_cast<WayID>(member.id));
			}
		}

		// relation_postscan_function may have changed the tags
		if (osmStore.scannedRelations.has_relation_tags(relid))
			readTags(osmStore.scannedRelations.relation_tags(relid), tags);
		else
			readTags(relation.tags, tags);

		if (!osmStore.usedRelations.count(relid) && !wayKeys.filter(tags)) continue;
		bool hasShape = !relation.shapeWkt.empty() && readRelationShape(relation.shapeWkt, shape);
		try {
			output.setRelation(relid, nodeMembers, outerWayVec, innerWayVec, tags, isMultiPolygon, isInnerOuter,
				hasShape ? &shape : nullptr);
		} catch (out_of_range& err) {
			// A member way is missing, most likely outside the area
			if (verbose) cerr << "Relation " << relid << ": " << err.what() << endl;
		}
	}
}

}

void ProcessOsmArea(
	const OsmArea& area,
	OSMStore& osmStore,
	OsmLuaProcessing& output,
	const SignificantTags& nodeKeys,
	const SignificantTags& wayKeys
) {
	unordered_set<WayID> usedWays;
	scanRelations(area, osmStore, output, wayKeys, usedWays);
	processNodes(area, osmStore, output, nodeKeys);
	processWays(area, osmStore, output, wayKeys, usedWays);
	processRelations(area, osmStore, output, wayKeys);
}
