#include <iostream>
#include "external/minunit.h"
#include "osm_store.h"

bool verbose = false;

MU_TEST(test_node_store) {
	NodeStore nodes;
	nodes.insert(1000010727889, LatpLon{507950000, -10950000});
	mu_check(nodes.contains(1000010727889));
	mu_check(!nodes.contains(1));
	mu_check(nodes.at(1000010727889).lon == -10950000);
	bool threw = false;
	try { nodes.at(1); } catch (std::out_of_range&) { threw = true; }
	mu_check(threw);
}

MU_TEST(test_way_store) {
	WayStore ways;
	ways.insert(55109820398, {LatpLon{1, 2}, LatpLon{3, 4}});
	mu_check(ways.contains(55109820398));
	mu_check(ways.at(55109820398).size() == 2);
	std::vector<LatpLon> out;
	ways.at(55109820398, out);
	mu_check(out[1].lon == 4);
	bool threw = false;
	try { ways.at(1); } catch (std::out_of_range&) { threw = true; }
	mu_check(threw);
}

// A square built from two ways that meet at their ends
MU_TEST(test_multipolygon_from_ways) {
	OSMStore store;
	const int d = 10000000;
	store.ways.insert(1, {LatpLon{0, 0}, LatpLon{0, d}, LatpLon{d, d}});
	store.ways.insert(2, {LatpLon{d, d}, LatpLon{d, 0}, LatpLon{0, 0}});
	WayVec outer = {1, 2}, inner;
	MultiPolygon mp = store.wayListMultiPolygon(outer.cbegin(), outer.cend(), inner.cbegin(), inner.cend());
	mu_check(mp.size() == 1);
	mu_check(std::abs(boost::geometry::area(mp) - 1.0) < 1e-9);
}

MU_TEST_SUITE(test_suite_osm_store) {
	MU_RUN_TEST(test_node_store);
	MU_RUN_TEST(test_way_store);
	MU_RUN_TEST(test_multipolygon_from_ways);
}

int main() {
	MU_RUN_SUITE(test_suite_osm_store);
	MU_REPORT();
	return MU_EXIT_CODE;
}
