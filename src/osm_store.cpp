/*
	OSM Store

	Store all of those to be output: latp/lon for nodes, node list for ways, and way list for relations.
	Only one instance of OSMStore is ever used. It will serve as the global data store. All data determined
	to be output will be set here, from tilemaker.cpp.

	OSMStore will be mainly used for geometry generation. Geometry generation logic is implemented in this class.
	These functions are used by osm_output, and can be used by osm_object to provide the geometry information to Lua.

	Internal data structures are encapsulated in NodeStore, WayStore and RelationStore classes.
	These store can be altered for efficient memory use without global code changes.
	Such data structures have to return const ForwardInputIterators (only *, ++ and == should be supported).

	Possible future improvements to save memory:
	- pack WayStore (e.g. zigzag PBF encoding and varint)
	- combine innerWays and outerWays into one vector, with a single-byte index marking the changeover
	- use two arrays (sorted keys and elements) instead of map
*/

//
// Views of data structures.
//

#include <stxxl/map>

template<class NodeIt>
struct NodeList {
	NodeIt begin;
	NodeIt end;
};

NodeList<NodeVec::const_iterator> makeNodeList(const NodeVec &nodeVec) {
	return { nodeVec.cbegin(), nodeVec.cend() };
}

template<class WayIt>
struct WayList {
	WayIt outerBegin;
	WayIt outerEnd;
	WayIt innerBegin;
	WayIt innerEnd;
};

WayList<WayVec::const_iterator> makeWayList( const WayVec &outerWayVec, const WayVec &innerWayVec) {
	return { outerWayVec.cbegin(), outerWayVec.cend(), innerWayVec.cbegin(), innerWayVec.cend() };
}

//
// Internal data structures.
//

#define DATA_NODE_BLOCK_SIZE (4096)
#define DATA_LEAF_BLOCK_SIZE (4096)

template <class T> class CompareGreater
{
public:
	bool operator () (const T& a, const T& b) const { return a > b; }
	static T max_value() { 
		// Counter intuitively, this returns the min value. Setting it to max
		// causes stxxl to segfault.
		return std::numeric_limits<T>::min(); 
	}
};
typedef CompareGreater<NodeID> CompareGreaterNodeID;

// Ordered map is used because it is higher performance in stxxl, although functionally
// we only need an unordered_map.
typedef stxxl::map<NodeID, LatpLon, CompareGreaterNodeID, DATA_NODE_BLOCK_SIZE, DATA_LEAF_BLOCK_SIZE> NodeStoreMap;

const stxxl::unsigned_type NODE_CACHE_SIZE_IN_BYTES = NodeStoreMap::node_block_type::raw_size * 100000;
const stxxl::unsigned_type LEAF_CACHE_SIZE_IN_BYTES = NodeStoreMap::leaf_block_type::raw_size * 100000;

// node store
class NodeStore {
	 NodeStoreMap mLatpLons;

public:
	NodeStore(): mLatpLons(NODE_CACHE_SIZE_IN_BYTES, LEAF_CACHE_SIZE_IN_BYTES) {

	}

	virtual ~NodeStore() {
	}

	// @brief Lookup a latp/lon pair
	// @param i OSM ID of a node
	// @return Latp/lon pair
	// @exception NotFound
	LatpLon at(NodeID i) const {
		NodeStoreMap::const_iterator it = mLatpLons.find(i);
		if(it == mLatpLons.end())
		{
			stringstream ss;
			ss << "Node " << i << " does not exist";
			throw std::out_of_range(ss.str());
		}
		NodeStoreMap::const_reference pair = *it;
		return pair.second;
	}

	// @brief Return whether a latp/lon pair is in the store.
	// @param i Any possible OSM ID
	// @return 1 if found, 0 otherwise
	// @note This function is named as count for consistent naming with stl functions.
	size_t count(NodeID i) const {
		NodeStoreMap::const_iterator it = mLatpLons.find(i);
		return it != mLatpLons.end();
	}

	// @brief Insert a latp/lon pair.
	// @param i OSM ID of a node
	// @param coord a latp/lon pair to be inserted
	// @invariant The OSM ID i must be larger than previously inserted OSM IDs of nodes
	//            (though unnecessarily for current impl, future impl may impose that)
	void insert_back(NodeID i, LatpLon coord) {
		mLatpLons.insert(std::pair<NodeID, LatpLon>(i, coord));
	}

	// @brief Make the store empty
	void clear() {
		mLatpLons.clear();
	}

	size_t size() { return mLatpLons.size(); }
};

// way store
typedef vector<NodeID>::const_iterator WayStoreIterator;

class WayStore {
	std::unordered_map<WayID, const vector<NodeID> > mNodeLists;

public:
	// @brief Lookup a node list
	// @param i OSM ID of a way
	// @return A node list
	// @exception NotFound
	NodeList<WayStoreIterator> at(WayID i) const {
		try {
			const auto &way = mNodeLists.at(i);
			return { way.cbegin(), way.cend() };
		}
		catch (std::out_of_range &err){
			stringstream ss;
			ss << "Could not find way " << i;
			throw std::out_of_range(ss.str());
		}
	}

	// @brief Return whether a node list is on the store.
	// @param i Any possible OSM ID
	// @return 1 if found, 0 otherwise
	// @note This function is named as count for consistent naming with stl functions.
	size_t count(WayID i) const {
		return mNodeLists.count(i);
	}

	// @brief Insert a node list.
	// @param i OSM ID of a way
	// @param nodeVec a node vector to be inserted
	// @invariant The OSM ID i must be larger than previously inserted OSM IDs of ways
	//            (though unnecessarily for current impl, future impl may impose that)
	void insert_back(WayID i, const NodeVec &nodeVec) {
		mNodeLists.emplace(i, nodeVec);
	}

	// @brief Make the store empty
	void clear() {
		mNodeLists.clear();
	}

	size_t size() { return mNodeLists.size(); }
};

// relation store
typedef vector<WayID>::const_iterator RelationStoreIterator;

class RelationStore {
	std::unordered_map<WayID, const pair<const vector<WayID>, const vector<WayID>>> mOutInLists;

public:
	// @brief Lookup a way list
	// @param i Pseudo OSM ID of a relational way
	// @return A way list
	// @exception NotFound
	WayList<RelationStoreIterator> at(WayID i) const {
		try
		{
			const auto &outInList = mOutInLists.at(i);
			return { outInList.first.cbegin(), outInList.first.cend(),
				outInList.second.cbegin(), outInList.second.cend() };
		}
		catch (std::out_of_range &err){
			stringstream ss;
			ss << "Could not find pseudo OSM ID of polygon " << i;
			throw std::out_of_range(ss.str());
		}
	}

	// @brief Return whether a way list is on the store.
	// @param i Any possible OSM ID
	// @return 1 if found, 0 otherwise
	// @note This function is named as count for consistent naming with stl functions.
	size_t count(WayID i) const {
		return mOutInLists.count(i);
	}

	// @brief Insert a way list.
	// @param i Pseudo OSM ID of a relation
	// @param outerWayVec A outer way vector to be inserted
	// @param innerWayVec A inner way vector to be inserted
	// @invariant The pseudo OSM ID i must be smaller than previously inserted pseudo OSM IDs of relations
	//            (though unnecessarily for current impl, future impl may impose that)
	void insert_front(WayID i, const WayVec &outerWayVec, const WayVec &innerWayVec) {
		mOutInLists.emplace(i, make_pair(outerWayVec, innerWayVec));
	}

	// @brief Make the store empty
	void clear() {
		mOutInLists.clear();
	}

	size_t size() { return mOutInLists.size(); }
};

//
// OSM store, containing all above.
//
struct OSMStore {
	NodeStore nodes;
	WayStore ways;
	RelationStore relations;

	void reportSize() {
		cout << "Stored " << nodes.size() << " nodes, " << ways.size() << " ways, " << relations.size() << " relations" << endl;
	}

	// Relation -> MultiPolygon
	template<class WayIt>
	MultiPolygon wayListMultiPolygon(WayList<WayIt> wayList) const {
		// polygon
		MultiPolygon mp;
		if (wayList.outerBegin != wayList.outerEnd) {
			// main outer way and inners
			Polygon poly;
			if (ways.count(*wayList.outerBegin)==1) { fillPoints(poly.outer(), ways.at(*wayList.outerBegin++)); }
			for (auto it = wayList.innerBegin; it != wayList.innerEnd; ++it) {
				Ring inner;
				if (ways.count(*it)==1) { fillPoints(inner, ways.at(*it)); }
				poly.inners().emplace_back(move(inner));
			}
			mp.emplace_back(move(poly));

			// additional outer ways - we don't match them up with inners, that shit is insane
			for (auto it = wayList.outerBegin; it != wayList.outerEnd; ++it) {
				Polygon outerPoly;
				if (ways.count(*it)==1) { fillPoints(outerPoly.outer(), ways.at(*it)); }
				mp.emplace_back(move(outerPoly));
			}

			// fix winding
			geom::correct(mp);
		}
		return mp;
	}

	MultiPolygon wayListMultiPolygon(WayID relId) const {
		return wayListMultiPolygon(relations.at(relId));
	}

	MultiPolygon wayListMultiPolygon(const WayVec &outerWayVec, const WayVec &innerWayVec) const {
		return wayListMultiPolygon(makeWayList(outerWayVec, innerWayVec));
	}

	// Way -> Polygon
	template<class NodeIt>
	Polygon nodeListPolygon(NodeList<NodeIt> nodeList) const {
		Polygon poly;
		fillPoints(poly.outer(), nodeList);
		geom::correct(poly);
		return poly;
	}

	Polygon nodeListPolygon(WayID wayId) const {
		return nodeListPolygon(ways.at(wayId));
	}

	Polygon nodeListPolygon(const NodeVec &nodeVec) const {
		return nodeListPolygon(makeNodeList(nodeVec));
	}

	// Way -> Linestring
	template<class NodeIt>
	Linestring nodeListLinestring(NodeList<NodeIt> nodeList) const {
		Linestring ls;
		fillPoints(ls, nodeList);
		return ls;
	}

	Linestring nodeListLinestring(WayID wayId) const {
		return nodeListLinestring(ways.at(wayId));
	}

	Linestring nodeListLinestring(const NodeVec &nodeVec) const {
		return nodeListLinestring(makeNodeList(nodeVec));
	}

private:
	// helper
	template<class PointRange, class NodeIt>
	void fillPoints(PointRange &points, NodeList<NodeIt> nodeList) const {
		for (auto it = nodeList.begin; it != nodeList.end; ++it) {
			LatpLon ll = nodes.at(*it);
			geom::range::push_back(points, geom::make<Point>(ll.lon/10000000.0, ll.latp/10000000.0));
		}
	}
};
