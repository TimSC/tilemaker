#include "tag_map.h"

void TagMap::addTag(const protozero::data_view& key, const protozero::data_view& value) {
	int64_t keyLoc = getKey(key.data(), key.size());
	if (keyLoc >= 0)
		tags[keyLoc].value = value;
	else
		tags.push_back({key, value});
}

int64_t TagMap::getKey(const char* key, size_t size) const {
	const protozero::data_view needle(key, size);
	for (size_t i = 0; i < tags.size(); i++)
		if (tags[i].key == needle)
			return i;
	return -1;
}

boost::container::flat_map<std::string, std::string> TagMap::exportToBoostMap() const {
	boost::container::flat_map<std::string, std::string> rv;
	for (const Tag& tag : tags)
		rv[std::string(tag.key.data(), tag.key.size())] = std::string(tag.value.data(), tag.value.size());
	return rv;
}
