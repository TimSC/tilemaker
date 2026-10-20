/*! \file */
#ifndef _OSM_AREA_H
#define _OSM_AREA_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The OSM data of the area being rendered, as read from a file. It is held whole in memory, so it should cover only that area:
// the ways and relations that reach into it, and all of their nodes.
//
// These types are deliberately tilemaker's own. cppo5m, which reads the data,
// is only included by osm_reader.cpp (its TagMap typedef would clash with ours).

using OsmAreaTags = std::vector<std::pair<std::string, std::string>>;

struct OsmAreaNode {
	int64_t id;
	double lat;
	double lon;
	OsmAreaTags tags;
};

struct OsmAreaWay {
	int64_t id;
	std::vector<int64_t> refs;
	OsmAreaTags tags;
};

struct OsmAreaMember {
	enum class Type : char { Node, Way, Relation };
	Type type;
	int64_t id;
	std::string role;
};

struct OsmAreaRelation {
	int64_t id;
	std::vector<OsmAreaMember> members;
	OsmAreaTags tags;
};

struct OsmArea {
	std::vector<OsmAreaNode> nodes;
	std::vector<OsmAreaWay> ways;
	std::vector<OsmAreaRelation> relations;

	// The bounds the input declared, if any (in degrees)
	bool hasBounds = false;
	double minLon = 0.0, minLat = 0.0, maxLon = 0.0, maxLat = 0.0;
};

// Adds the contents of an .osm, .o5m, .pbf or .json file to area. The format
// is taken from the file's extension. Throws std::runtime_error on failure.
void ReadOsmFile(const std::string& filename, OsmArea& area);

#endif //_OSM_AREA_H
