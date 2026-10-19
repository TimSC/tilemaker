#include "attribute_store.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

uint16_t AttributeKeyStore::key2index(const std::string& key) {
	auto it = keys2index.find(key);
	if (it != keys2index.end())
		return it->second;

	if (keys.size() > UINT16_MAX)
		throw std::out_of_range("too many attribute keys");
	uint16_t newIndex = keys.size();
	keys.push_back(key);
	keys2index[key] = newIndex;
	return newIndex;
}

uint32_t AttributePairStore::addPair(const AttributePair& pair) {
	auto it = pairIndexes.find(pair);
	if (it != pairIndexes.end())
		return it->second;

	uint32_t index = pairs.size();
	pairs.push_back(pair);
	pairIndexes[pair] = index;
	return index;
}

void AttributeSet::removePairWithKey(const AttributePairStore& pairStore, uint32_t keyIndex) {
	for (size_t i = 0; i < pairs.size(); i++) {
		if (pairStore.getPair(pairs[i]).keyIndex == keyIndex) {
			pairs.erase(pairs.begin() + i);
			return;
		}
	}
}

void AttributeSet::finalize() {
	std::sort(pairs.begin(), pairs.end());
}

void AttributeStore::addAttribute(AttributeSet& attributeSet, std::string const &key, const protozero::data_view v, char minzoom) {
	addAttribute(attributeSet, key, std::string(v.data(), v.size()), minzoom);
}
void AttributeStore::addAttribute(AttributeSet& attributeSet, std::string const &key, const std::string& v, char minzoom) {
	attributeSet.addPair(pairStore.addPair(AttributePair(keyStore.key2index(key), v, minzoom)));
}
void AttributeStore::addAttribute(AttributeSet& attributeSet, std::string const &key, bool v, char minzoom) {
	attributeSet.addPair(pairStore.addPair(AttributePair(keyStore.key2index(key), v, minzoom)));
}
void AttributeStore::addAttribute(AttributeSet& attributeSet, std::string const &key, double v, char minzoom) {
	attributeSet.addPair(pairStore.addPair(AttributePair(keyStore.key2index(key), v, minzoom)));
}
void AttributeStore::addAttribute(AttributeSet& attributeSet, std::string const &key, int v, char minzoom) {
	attributeSet.addPair(pairStore.addPair(AttributePair(keyStore.key2index(key), v, minzoom)));
}

AttributeIndex AttributeStore::add(AttributeSet &attributes) {
	attributes.finalize();
	auto it = setIndexes.find(attributes);
	if (it != setIndexes.end())
		return it->second;

	AttributeIndex index = sets.size();
	sets.push_back(attributes);
	setIndexes[attributes] = index;
	return index;
}

std::vector<const AttributePair*> AttributeStore::getUnsafe(AttributeIndex index) const {
	if (index >= sets.size())
		throw std::runtime_error("Failed to fetch attributes at index " + std::to_string(index));

	std::vector<const AttributePair*> rv;
	for (uint32_t pair : sets[index].pairs)
		rv.push_back(&pairStore.getPairUnsafe(pair));
	return rv;
}

void AttributeStore::reportSize() const {
	std::cout << "Attributes: " << size() << " sets" << std::endl;
}
