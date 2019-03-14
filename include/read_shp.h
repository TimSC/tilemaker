/*! \file */ 
#ifndef _READ_SHP_H
#define _READ_SHP_H

#include <unordered_map>
#include <string>
#include <vector>
#include <map>
#include "geomtypes.h"

// Shapelib
#include "shapefil.h"

typedef boost::variant< int, double, std::string > ShpFieldValue;
typedef std::map<std::string, ShpFieldValue> ShpFieldValueMap;

class ShapeFileResultsDecoder
{
public:
	ShapeFileResultsDecoder() {};
	virtual ~ShapeFileResultsDecoder() {};	

	virtual void AddObject(int i, enum OutputGeometryType geomType,
		Geometry geometry, bool hasName, const std::string &name, const ShpFieldValueMap &keyVals) {};

	virtual void FoundColumn(const std::string &key, int typeVal) {};
};

void prepareShapefile(const std::string &filename,
		const std::vector<std::string> &columns,
		class ShapeFileResultsDecoder &outObj);

/// Read shapefile, and produce call back events for all objects within the specified bounding box
void readShapefile(const Box &clippingBox,
					const std::string &filename,
					const std::vector<std::string> &columns,
					const std::string &indexName,
					class ShapeFileResultsDecoder &outObj);

#endif //_READ_SHP_H

