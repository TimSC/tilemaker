#include "layer.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

using namespace std;
using namespace rapidjson;

// Define a layer (as read from the .json file)
uint LayerDefinition::addLayer(string name, uint minzoom, uint maxzoom,
		uint simplifyBelow, double simplifyLevel, double simplifyLength, double simplifyRatio, 
		const std::string &source,
		const std::vector<std::string> &sourceColumns,
		bool indexed,
		const std::string &indexName,		
		const std::string &writeTo) 
{
	LayerDef layer = { name, minzoom, maxzoom, simplifyBelow, simplifyLevel, simplifyLength, simplifyRatio, 
		source, sourceColumns, indexed, indexName,
		std::map<std::string,uint>() };
	layers.push_back(layer);
	uint layerNum = layers.size()-1;
	layerMap[name] = layerNum;

	if (writeTo.empty()) {
		vector<uint> r = { layerNum };
		layerOrder.push_back(r);
	} else {
		if (layerMap.count(writeTo) == 0) {
			throw out_of_range("ERROR: addLayer(): the layer to write, named as \"" + writeTo + "\", doesn't exist.");
		}
		uint lookingFor = layerMap[writeTo];
		for (auto it = layerOrder.begin(); it!= layerOrder.end(); ++it) {
			if (it->at(0)==lookingFor) {
				it->push_back(layerNum);
			}
		}
	}
	return layerNum;
}

std::string LayerDefinition::serialiseToJSON() {
	Document document;
	document.SetObject();
	Document::AllocatorType& allocator = document.GetAllocator();

	Value layerArray(kArrayType);
	for (auto it = layers.begin(); it != layers.end(); ++it) {
		Value fieldObj(kObjectType);
		for (auto jt = it->attributeMap.begin(); jt != it->attributeMap.end(); ++jt) {
			Value k(jt->first.c_str(), allocator);
			switch (jt->second) {
				case 0: fieldObj.AddMember(k, "String" , allocator); break;
				case 1:	fieldObj.AddMember(k, "Number" , allocator); break;
				case 2:	fieldObj.AddMember(k, "Boolean", allocator); break;
			}
		}
		Value layerObj(kObjectType);
		Value name(it->name.c_str(), allocator);
		layerObj.AddMember("id",      name,        allocator);
		layerObj.AddMember("fields",  fieldObj,    allocator);
		layerObj.AddMember("minzoom", it->minzoom, allocator);
		layerObj.AddMember("maxzoom", it->maxzoom, allocator);
		layerArray.PushBack(layerObj, allocator);
	}

	document.AddMember("vector_layers", layerArray, allocator);

	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	document.Accept(writer);
	string json(buffer.GetString(), buffer.GetSize());
	return json;
}


// *****************************************************************

