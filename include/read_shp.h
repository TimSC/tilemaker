/*! \file */ 
#ifndef _READ_SHP_H
#define _READ_SHP_H

#include <unordered_map>
#include <string>
#include <vector>
#include <map>
#include "geomtypes.h"
#include "output_object.h"
#include "layer.h"

// Shapelib
#include "shapefil.h"

class ShapeFileResultsDecoder
{
public:
	ShapeFileResultsDecoder() {};
	virtual ~ShapeFileResultsDecoder() {};	

	virtual OutputObjectRef AddObject(const class LayerDef &layer, uint_least8_t layerNum,
		enum OutputGeometryType geomType,
		Geometry geometry, bool hasName, const std::string &name) 
		{OutputObjectRef empty; return empty;};
};

void prepareShapefile(class LayerDefinition &layers,
                   uint baseZoom, uint layerNum);

/// Read shapefile, and create OutputObjects for all objects within the specified bounding box
void readShapefile(const Box &clippingBox,
                   const class LayerDefinition &layers,
                   uint baseZoom, uint layerNum,
				   class ShapeFileResultsDecoder &outObj);

#endif //_READ_SHP_H

