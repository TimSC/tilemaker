/*! \file */ 
#ifndef _READ_SHP_H
#define _READ_SHP_H

#include <unordered_map>
#include <string>
#include <vector>
#include <map>
#include "geomtypes.h"
#include "layer.h"

// Shapelib
#include "shapefil.h"

///\brief Specifies geometry type for an OutputObject
enum OutputGeometryType { POINT, LINESTRING, POLYGON, CENTROID, CACHED_POINT, CACHED_LINESTRING, CACHED_POLYGON };

typedef boost::variant< int, double, std::string > ShpFieldValue;
typedef std::map<std::string, ShpFieldValue> ShpFieldValueMap;

class ShapeFileResultsDecoder
{
public:
	ShapeFileResultsDecoder() {};
	virtual ~ShapeFileResultsDecoder() {};	

	virtual void AddObject(const class LayerDef &layer, uint_least8_t layerNum,
		enum OutputGeometryType geomType,
		Geometry geometry, bool hasName, const std::string &name, const ShpFieldValueMap &keyVals) {};

	virtual void FoundColumn(const std::string &key, int typeVal) {};
};

void prepareShapefile(const std::string &filename,
		const std::vector<std::string> &columns,
		class ShapeFileResultsDecoder &outObj);

/// Read shapefile, and create OutputObjects for all objects within the specified bounding box
void readShapefile(const Box &clippingBox,
                   const class LayerDefinition &layers,
                   uint layerNum,
				   class ShapeFileResultsDecoder &outObj);

#endif //_READ_SHP_H

