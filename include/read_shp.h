/*! \file */ 
#ifndef _READ_SHP_H
#define _READ_SHP_H

#include <unordered_map>
#include <string>
#include <vector>
#include <map>
#include "geomtypes.h"
#include "output_object.h"
#include "osm_lua_processing.h"
#include "kaguya.hpp"

// Shapelib
#include "shapefil.h"

void fillPointArrayFromShapefile(std::vector<Point> *points, SHPObject *shape, uint part);

void prepareShapefile(class LayerDefinition &layers,
                   uint baseZoom, uint layerNum,
				   OsmLuaProcessing &osmLuaProcessing);

/// Read shapefile, and create OutputObjects for all objects within the specified bounding box
void readShapefile(const Box &clippingBox,
                   const class LayerDefinition &layers,
                   uint baseZoom, uint layerNum,
				   class TileIndexCached &outObj,
				   OsmLuaProcessing &osmLuaProcessing);

#endif //_READ_SHP_H

