// The only file that includes cppo5m: its model.h declares a global TagMap
// typedef, which would clash with tilemaker's TagMap class.
#include <fstream>
#include <stdexcept>
#include "io.h" // cppo5m
#include "osm_area.h"

namespace {

OsmAreaTags copyTags(const std::map<std::string, std::string>& tags) {
	return OsmAreaTags(tags.begin(), tags.end());
}

OsmAreaMember::Type memberType(ObjectType type) {
	switch (type) {
		case ObjectType::Node: return OsmAreaMember::Type::Node;
		case ObjectType::Way: return OsmAreaMember::Type::Way;
		default: return OsmAreaMember::Type::Relation;
	}
}

// Collects a decoded stream into an OsmArea
class OsmAreaHandler : public IDataStreamHandler {
public:
	explicit OsmAreaHandler(OsmArea& area): area(area) {}

	void StoreBounds(const Bounds& bounds) override {
		if (area.hasBounds) {
			area.minLon = std::min(area.minLon, bounds.minLon);
			area.minLat = std::min(area.minLat, bounds.minLat);
			area.maxLon = std::max(area.maxLon, bounds.maxLon);
			area.maxLat = std::max(area.maxLat, bounds.maxLat);
		} else {
			area.hasBounds = true;
			area.minLon = bounds.minLon;
			area.minLat = bounds.minLat;
			area.maxLon = bounds.maxLon;
			area.maxLat = bounds.maxLat;
		}
	}

	void StoreNode(const OsmNode& node) override {
		area.nodes.push_back({node.objId, node.lat, node.lon, copyTags(node.tags)});
	}

	void StoreWay(const OsmWay& way) override {
		area.ways.push_back({way.objId, way.refs, copyTags(way.tags)});
	}

	void StoreRelation(const OsmRelation& relation) override {
		OsmAreaRelation out{relation.objId, {}, copyTags(relation.tags)};
		out.members.reserve(relation.members.size());
		for (const RelationMember& member : relation.members)
			out.members.push_back({memberType(member.type), member.ref, member.role});
		area.relations.push_back(std::move(out));
	}

private:
	OsmArea& area;
};

}

void ReadOsmFile(const std::string& filename, OsmArea& area) {
	OsmFormat format;
	try {
		format = FormatFromFilename(filename);
	} catch (const std::invalid_argument& e) {
		throw std::runtime_error(e.what());
	}
	if (filename.size() > 3 && filename.compare(filename.size() - 3, 3, ".gz") == 0)
		throw std::runtime_error("Compressed input is not supported: " + filename);

	std::filebuf file;
	if (!file.open(filename, std::ios::in | std::ios::binary))
		throw std::runtime_error("Couldn't open " + filename);

	OsmAreaHandler handler(area);
	try {
		MakeDecoder(format, file, handler)->Decode();
	} catch (const OsmDecodeError& e) {
		throw std::runtime_error("Couldn't read " + filename + ": " + e.what());
	}
}
