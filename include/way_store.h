#ifndef _WAY_STORE_H
#define _WAY_STORE_H

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "coordinates.h"

// The node locations of every way in the input that has them all (or, without
// integrity checks, any). As NodeStore, a plain hash map filled on one thread.
class WayStore
{
public:
	void insert(WayID id, std::vector<LatpLon> lls) { latpLonLists[id] = std::move(lls); }
	void clear() { latpLonLists.clear(); }

	size_t size() const { return latpLonLists.size(); }
	bool contains(WayID id) const { return latpLonLists.count(id) > 0; }
	const std::vector<LatpLon>& at(WayID id) const {
		auto it = latpLonLists.find(id);
		if (it == latpLonLists.end())
			throw std::out_of_range("Could not find way with id " + std::to_string(id));
		return it->second;
	}
	void at(WayID id, std::vector<LatpLon>& output) const { output = at(id); }

private:
	std::unordered_map<WayID, std::vector<LatpLon>> latpLonLists;
};

#endif
