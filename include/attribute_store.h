/*! \file */
#ifndef _ATTRIBUTE_STORE_H
#define _ATTRIBUTE_STORE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <protozero/data_view.hpp>

/* AttributeStore - dictionary of the attributes of output objects

   Keys, key/value pairs and whole sets of pairs are each kept once and
   referred to by index. The input covers a small area, so plain vectors and
   maps are enough. */

typedef uint32_t AttributeIndex;

class AttributeKeyStore {
public:
	uint16_t key2index(const std::string& key);
	const std::string& getKey(uint16_t index) const { return keys.at(index); }
	const std::string& getKeyUnsafe(uint16_t index) const { return keys[index]; }

private:
	std::vector<std::string> keys = {""}; // index 0 is not used
	std::map<std::string, uint16_t> keys2index;
};

enum class AttributePairType: uint8_t { String = 0, Float = 1, Bool = 2, Int = 3 };

// AttributePair is a key/value pair (with minzoom)
struct AttributePair {
	uint16_t keyIndex;
	AttributePairType valueType;
	uint8_t minzoom;
	std::string stringValue_;
	double doubleValue_ = 0; // floats, ints and bools

	AttributePair(uint32_t keyIndex, bool value, char minzoom)
		: keyIndex(keyIndex), valueType(AttributePairType::Bool), minzoom(minzoom), doubleValue_(value ? 1 : 0) {}
	AttributePair(uint32_t keyIndex, const std::string& value, char minzoom)
		: keyIndex(keyIndex), valueType(AttributePairType::String), minzoom(minzoom), stringValue_(value) {}
	AttributePair(uint32_t keyIndex, double value, char minzoom)
		: keyIndex(keyIndex), valueType(AttributePairType::Float), minzoom(minzoom), doubleValue_(value) {}
	AttributePair(uint32_t keyIndex, int value, char minzoom)
		: keyIndex(keyIndex), valueType(AttributePairType::Int), minzoom(minzoom), doubleValue_(static_cast<double>(value)) {}

	bool operator<(const AttributePair& other) const {
		if (minzoom != other.minzoom) return minzoom < other.minzoom;
		if (keyIndex != other.keyIndex) return keyIndex < other.keyIndex;
		if (valueType != other.valueType) return valueType < other.valueType;
		if (hasStringValue()) return stringValue_ < other.stringValue_;
		return doubleValue_ < other.doubleValue_;
	}

	bool operator==(const AttributePair &other) const {
		return !(*this < other) && !(other < *this);
	}

	bool hasStringValue() const { return valueType == AttributePairType::String; }
	bool hasFloatValue() const { return valueType == AttributePairType::Float; }
	bool hasBoolValue() const { return valueType == AttributePairType::Bool; }
	bool hasIntValue() const { return valueType == AttributePairType::Int; }

	const std::string& stringValue() const { return stringValue_; }
	float floatValue() const { return static_cast<float>(doubleValue_); }
	bool boolValue() const { return doubleValue_; }
	int intValue() const { return static_cast<int64_t>(doubleValue_); }
};

class AttributePairStore {
public:
	uint32_t addPair(const AttributePair& pair);
	const AttributePair& getPair(uint32_t i) const { return pairs.at(i); }
	const AttributePair& getPairUnsafe(uint32_t i) const { return pairs[i]; }

private:
	std::vector<AttributePair> pairs;
	std::map<AttributePair, uint32_t> pairIndexes;
};

// AttributeSet is a set of AttributePairs (by index)
// = the complete attributes for one object
struct AttributeSet {
	std::vector<uint32_t> pairs;

	void addPair(uint32_t index) { pairs.push_back(index); }
	// When adding a new key/value, we need to remove any existing pair that has that key.
	void removePairWithKey(const AttributePairStore& pairStore, uint32_t keyIndex);
	// Sorts the pairs, giving a canonical form to compare
	void finalize();

	size_t numPairs() const { return pairs.size(); }
	uint32_t getPair(size_t i) const { return pairs[i]; }

	bool operator<(const AttributeSet& other) const { return pairs < other.pairs; }
	bool operator==(const AttributeSet& other) const { return pairs == other.pairs; }
	bool operator!=(const AttributeSet& other) const { return pairs != other.pairs; }
};

// AttributeStore is the store for all AttributeSets
struct AttributeStore {
	AttributeIndex add(AttributeSet &attributes);
	std::vector<const AttributePair*> getUnsafe(AttributeIndex index) const;
	size_t size() const { return sets.size(); }
	void reportSize() const;

	void addAttribute(AttributeSet& attributeSet, std::string const &key, const protozero::data_view v, char minzoom);
	void addAttribute(AttributeSet& attributeSet, std::string const &key, const std::string& v, char minzoom);
	// (or a char* would be taken as a bool)
	void addAttribute(AttributeSet& attributeSet, std::string const &key, const char* v, char minzoom) { addAttribute(attributeSet, key, std::string(v), minzoom); }
	void addAttribute(AttributeSet& attributeSet, std::string const &key, double v, char minzoom);
	void addAttribute(AttributeSet& attributeSet, std::string const &key, int v, char minzoom);
	void addAttribute(AttributeSet& attributeSet, std::string const &key, bool v, char minzoom);

	AttributeKeyStore keyStore;
	AttributePairStore pairStore;

private:
	std::vector<AttributeSet> sets;
	std::map<AttributeSet, AttributeIndex> setIndexes;
};

#endif //_ATTRIBUTE_STORE_H
