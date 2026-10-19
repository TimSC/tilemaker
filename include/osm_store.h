/*! \file */ 
#ifndef _OSM_STORE_H
#define _OSM_STORE_H

#include "geom.h"
#include "coordinates.h"
#include "node_store.h"
#include "way_store.h"
#include "relation_roles.h"

#include <utility>
#include <vector>
#include <set>
#include <unordered_set>
#include <boost/container/flat_map.hpp>
#include <protozero/data_view.hpp>

extern bool verbose;

// A comparator for data_view so it can be used in boost's flat_map
struct DataViewLessThan {
	bool operator()(const protozero::data_view& a, const protozero::data_view& b) const {
		return a < b;
	}
};


// scanned relations store
class RelationScanStore {

private:
	using tag_map_t = boost::container::flat_map<std::string, std::string>;
	std::map<WayID, std::vector<std::pair<RelationID, uint16_t>>> relationsForWays;
	std::map<NodeID, std::vector<std::pair<RelationID, uint16_t>>> relationsForNodes;
	std::map<RelationID, tag_map_t> relationTags;
	RelationRoles relationRoles;

public:
	std::map<RelationID, std::vector<std::pair<RelationID, uint16_t>>> relationsForRelations;

	void relation_contains_way(RelationID relid, WayID wayid, std::string role) {
		relationsForWays[wayid].emplace_back(relid, relationRoles.getOrAddRole(role));
	}
	void relation_contains_node(RelationID relid, NodeID nodeId, std::string role) {
		relationsForNodes[nodeId].emplace_back(relid, relationRoles.getOrAddRole(role));
	}
	void relation_contains_relation(RelationID relid, RelationID relationId, std::string role) {
		relationsForRelations[relationId].emplace_back(relid, relationRoles.getOrAddRole(role));
	}
	void store_relation_tags(RelationID relid, const tag_map_t &tags) {
		relationTags[relid] = tags;
	}
	void set_relation_tag(RelationID relid, const std::string &key, const std::string &value) {
		relationTags[relid][key] = value;
	}
	bool way_in_any_relations(WayID wayid) {
		return relationsForWays.find(wayid) != relationsForWays.end();
	}
	bool node_in_any_relations(NodeID nodeId) {
		return relationsForNodes.find(nodeId) != relationsForNodes.end();
	}
	bool relation_in_any_relations(RelationID relId) {
		return relationsForRelations.find(relId) != relationsForRelations.end();
	}
	std::string getRole(uint16_t roleId) const { return relationRoles.getRole(roleId); }
	const std::vector<std::pair<WayID, uint16_t>>& relations_for_way(WayID wayid) {
		return relationsForWays[wayid];
	}
	const std::vector<std::pair<RelationID, uint16_t>>& relations_for_node(NodeID nodeId) {
		return relationsForNodes[nodeId];
	}
	const std::vector<std::pair<RelationID, uint16_t>>& relations_for_relation(RelationID relId) {
		return relationsForRelations[relId];
	}
	bool has_relation_tags(RelationID relId) {
		return relationTags.find(relId) != relationTags.end();
	}
	const tag_map_t& relation_tags(RelationID relId) {
		return relationTags[relId];
	}
	// return all the parent relations (and their parents &c.) for a given relation
	std::vector<std::pair<RelationID, uint16_t>> relations_for_relation_with_parents(RelationID relId) {
		std::vector<RelationID> relationsToDo;
		std::set<RelationID> relationsDone;
		std::vector<std::pair<WayID, uint16_t>> out;
		relationsToDo.emplace_back(relId);
		// check parents in turn, pushing onto the stack if necessary
		while (!relationsToDo.empty()) {
			RelationID rel = relationsToDo.back();
			relationsToDo.pop_back();
			// check it's not already been added
			if (relationsDone.find(rel) != relationsDone.end()) continue;
			relationsDone.insert(rel);
			// add all its parents
			for (auto rp : relationsForRelations[rel]) {
				out.emplace_back(rp);
				relationsToDo.emplace_back(rp.first);
			}
		}
		return out;
	}
	std::string get_relation_tag(RelationID relid, const std::string &key) {
		auto it = relationTags.find(relid);
		if (it==relationTags.end()) return "";
		auto jt = it->second.find(key);
		if (jt==it->second.end()) return "";
		return jt->second;
	}
};


/**
	\brief OSM store keeps nodes, ways and relations in memory for later access

	Store all of those to be output: latp/lon for nodes, node list for ways, and way list for relations.
	It will serve as the global data store. OSM data destined for output will be set here from OsmMemTiles.

	The input covers only the area being rendered, so everything in it is kept.
*/
class OSMStore
{
public:
	NodeStore nodes;
	WayStore ways;
	RelationScanStore scannedRelations;

	// Relations accepted by relation_scan_function, which relation_function is then run on
	std::unordered_set<RelationID> usedRelations;

protected:
	bool require_integrity = true;

public:
	void enforce_integrity(bool ei) { require_integrity = ei; }
	bool integrity_enforced() { return require_integrity; }

	using tag_map_t = boost::container::flat_map<std::string, std::string>;

	void clear();
	void reportSize() const;

	// Relation -> MultiPolygon or MultiLinestring
	MultiPolygon wayListMultiPolygon(WayVec::const_iterator outerBegin, WayVec::const_iterator outerEnd, WayVec::const_iterator innerBegin, WayVec::const_iterator innerEnd) const;
	MultiLinestring wayListMultiLinestring(WayVec::const_iterator outerBegin, WayVec::const_iterator outerEnd) const;
	void mergeMultiPolygonWays(std::vector<LatpLonDeque> &results, std::map<WayID,bool> &done, WayVec::const_iterator itBegin, WayVec::const_iterator itEnd) const;

	///It is not really meaningful to try using a relation as a linestring. Not normally used but included
	///if Lua script attempts to do this.
	//
	// Relation -> MultiPolygon
	static Linestring wayListLinestring(MultiPolygon const &mp) {
		Linestring out;
		if(!mp.empty()) {
			for(auto pt: mp[0].outer())
				boost::geometry::append(out, pt);
		}
		return out;
	}

	template<class WayIt>
	Polygon llListPolygon(WayIt begin, WayIt end) const {
		Polygon poly;
		poly.outer().reserve(end - begin);
		fillPoints(poly.outer(), begin, end);
		boost::geometry::correct(poly);
		return poly;
	}

	// Way -> Linestring
	template<class WayIt>
	Linestring llListLinestring(WayIt begin, WayIt end) const {
		Linestring ls;
		ls.reserve(end - begin);
		fillPoints(ls, begin, end);
		return ls;
	}

private:
	// helper
	template<class PointRange, class LatpLonIt>
	void fillPoints(PointRange &points, LatpLonIt begin, LatpLonIt end) const {
		for (auto it = begin; it != end; ++it) {
			try {
				boost::geometry::range::push_back(points, boost::geometry::make<Point>(it->lon/10000000.0, it->latp/10000000.0));
			} catch (std::out_of_range &err) {
				if (require_integrity) throw err;
			}
		}
	}
};

#endif //_OSM_STORE_H
