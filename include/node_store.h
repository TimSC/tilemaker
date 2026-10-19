#ifndef _NODE_STORE_H
#define _NODE_STORE_H

#include <stdexcept>
#include <string>
#include <unordered_map>
#include "coordinates.h"

// The location of every node in the input. The input covers a small area,
// so this is a plain hash map. It is filled on one thread, then only read.
class NodeStore
{
public:
	void insert(NodeID id, LatpLon ll) { latpLons[id] = ll; }
	void clear() { latpLons.clear(); }

	size_t size() const { return latpLons.size(); }
	bool contains(NodeID id) const { return latpLons.count(id) > 0; }
	LatpLon at(NodeID id) const {
		auto it = latpLons.find(id);
		if (it == latpLons.end())
			throw std::out_of_range("Could not find node with id " + std::to_string(id));
		return it->second;
	}

private:
	std::unordered_map<NodeID, LatpLon> latpLons;
};

#endif
