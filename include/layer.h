#ifndef _LAYER_H
#define _LAYER_H

#include <string>
#include <map>
#include <vector>

///\brief Defines map single layer appearance
class LayerDef 
{
public:
	std::string name;
	uint minzoom;
	uint maxzoom;
	uint simplifyBelow;
	double simplifyLevel;
	double simplifyLength;
	double simplifyRatio;
	std::string source;
	std::vector<std::string> sourceColumns;
	bool indexed;
	std::string indexName;
	std::map<std::string, uint> attributeMap;
};

///\brief Defines layers used in map rendering
class LayerDefinition
{
public:	
	std::vector<LayerDef> layers;				// List of layers
	std::map<std::string,uint> layerMap;				// Layer->position map
	std::vector<std::vector<uint> > layerOrder;		// Order of (grouped) layers, e.g. [ [0], [1,2,3], [4] ]

	// Define a layer (as read from the .json file)
	uint addLayer(std::string name, uint minzoom, uint maxzoom,
			uint simplifyBelow, double simplifyLevel, double simplifyLength, double simplifyRatio, 
			const std::string &source,
			const std::vector<std::string> &sourceColumns,
			bool indexed,
			const std::string &indexName,	
			const std::string &writeTo);

	std::string serialiseToJSON();
};

#endif //_LAYER_H

