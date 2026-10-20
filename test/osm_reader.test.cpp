#include <fstream>
#include <iostream>
#include "external/minunit.h"
#include "osm_area.h"

const char* TEST_OSM = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
  <bounds minlat="50.795" minlon="-1.095" maxlat="50.8" maxlon="-1.085"/>
  <node id="1000010727889" lat="50.796" lon="-1.09"><tag k="amenity" v="pub"/></node>
  <node id="2" lat="50.797" lon="-1.091"/>
  <way id="55109820398"><nd ref="1000010727889"/><nd ref="2"/><tag k="highway" v="residential"/></way>
  <relation id="7"><member type="way" ref="55109820398" role="outer"/><member type="node" ref="2" role="label"/><tag k="type" v="multipolygon"/></relation>
</osm>
)";

MU_TEST(test_read_osm_file) {
	const std::string filename = "osm_reader_test.osm";
	{
		std::ofstream out(filename);
		out << TEST_OSM;
	}
	OsmArea area;
	ReadOsmFile(filename, area);
	std::remove(filename.c_str());

	mu_check(area.hasBounds);
	mu_check(area.minLon == -1.095 && area.maxLat == 50.8);

	mu_check(area.nodes.size() == 2);
	mu_check(area.nodes[0].id == 1000010727889);
	mu_check(area.nodes[0].tags.size() == 1);
	mu_check(area.nodes[0].tags[0].first == "amenity" && area.nodes[0].tags[0].second == "pub");
	mu_check(area.nodes[1].tags.empty());

	mu_check(area.ways.size() == 1);
	mu_check(area.ways[0].refs.size() == 2 && area.ways[0].refs[1] == 2);

	mu_check(area.relations.size() == 1);
	const auto& members = area.relations[0].members;
	mu_check(members.size() == 2);
	mu_check(members[0].type == OsmAreaMember::Type::Way && members[0].role == "outer");
	mu_check(members[1].type == OsmAreaMember::Type::Node && members[1].id == 2 && members[1].role == "label");
}

MU_TEST(test_unknown_extension) {
	OsmArea area;
	bool threw = false;
	try { ReadOsmFile("area.shp", area); } catch (std::runtime_error&) { threw = true; }
	mu_check(threw);
}

MU_TEST_SUITE(test_suite_osm_reader) {
	MU_RUN_TEST(test_read_osm_file);
	MU_RUN_TEST(test_unknown_extension);
}

int main() {
	MU_RUN_SUITE(test_suite_osm_reader);
	MU_REPORT();
	return MU_EXIT_CODE;
}
