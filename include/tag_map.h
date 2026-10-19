#ifndef _TAG_MAP_H
#define _TAG_MAP_H

#include <vector>
#include <string>
#include <boost/container/flat_map.hpp>
#include <protozero/data_view.hpp>

// The tags of the object being processed, for Lua. The keys and values are
// views: the strings they view must outlive the map's use. OSM objects have
// few tags, so they are kept in a list and found by searching it.
struct Tag {
	protozero::data_view key;
	protozero::data_view value;
};

class TagMap {
public:
	void reset() { tags.clear(); }
	bool empty() const { return tags.empty(); }
	// Adds a tag, or replaces the value of a key already added
	void addTag(const protozero::data_view& key, const protozero::data_view& value);

	// Return -1 if key not found, else its position, for getValueFromKey
	int64_t getKey(const char* key, size_t size) const;
	const protozero::data_view* getValueFromKey(uint32_t keyLoc) const { return &tags[keyLoc].value; }

	boost::container::flat_map<std::string, std::string> exportToBoostMap() const;

	std::vector<Tag>::const_iterator begin() const { return tags.begin(); }
	std::vector<Tag>::const_iterator end() const { return tags.end(); }

private:
	std::vector<Tag> tags;
};

#endif //_TAG_MAP_H
