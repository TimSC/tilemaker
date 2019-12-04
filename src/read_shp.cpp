#include "read_shp.h"
#include "coordinates.h"
#include <iostream>

using namespace std;
namespace geom = boost::geometry;

/*
	Read shapefiles into Boost geometries
*/

void fillPointArrayFromShapefile(vector<Point> *points, SHPObject *shape, uint part) {
	uint start = shape->panPartStart[part];
	uint end   = (int(part)==shape->nParts-1) ? shape->nVertices : shape->panPartStart[part+1];
    double* const x = shape->padfX;
    double* const y = shape->padfY;
	points->clear(); if (points->capacity() < (end-start)+1) { points->reserve(end-start+1); }
	double prevx = 1000;
	double prevy = 1000;
	for (uint i=start; i<end; i++) {
		y[i] = fmin(fmax(y[i], MinLat),MaxLat);	// To avoid infinite latp
		double latp = lat2latp(y[i]);
		// skip duplicated point
		if ((i == end - 1 && (x[i] != prevx || latp != prevy)) ||
		    (fabs(x[i] - prevx) >= 0.00000001 || fabs(latp - prevy) >= 0.00000001)) {
			points->emplace_back(geom::make<Point>(x[i], latp));
			prevx = x[i];
			prevy = latp;
		}
		points->emplace_back(geom::make<Point>(x[i], lat2latp(y[i])));
	}
}

void setLayerColumnTypes(
		DBFHandle &dbf,
		const unordered_map<int,string> &columnMap, const unordered_map<int,int> &columnTypeMap,
		class ShapeFileResultsDecoder &outObj) {

	for (auto it : columnMap) {
		int pos = it.first;
		string key = it.second;
		auto ctmIter = columnTypeMap.find(pos);
		if(ctmIter == columnTypeMap.end()) throw runtime_error("ctmIter is null");
		int typeVal = 0;
		switch (ctmIter->second) {
			case FTInteger:
			     typeVal = 1;
			     break;
			case FTDouble:
			     typeVal = 1;
			     break;
			default:
			     typeVal = 3;
			     break;
		}

		outObj.FoundColumn(key, typeVal);
	}
}

// Read requested attributes from a shapefile, and encode into an OutputObject
void GetShapefileAttributes(DBFHandle &dbf,
		int recordNum, const unordered_map<int,string> &columnMap,
		const unordered_map<int,int> &columnTypeMap,
		map<string, ShpFieldValue> &keyVals) 
{
	for (auto it : columnMap) {
		const int &pos = it.first;
		const string &key = it.second;
		ShpFieldValue v;
		auto ctmIter = columnTypeMap.find(pos);
		switch (ctmIter->second) {
			case FTInteger:  
			         v = DBFReadIntegerAttribute(dbf, recordNum, pos);
			         break;
			case FTDouble:  
			         v = DBFReadDoubleAttribute(dbf, recordNum, pos);
			         break;
			default: 
			         v = DBFReadStringAttribute(dbf, recordNum, pos);
			         break;
		}
		keyVals[key] = v;
	}
}

void GetColumnMaps(DBFHandle dbf, const vector<string> &columns,
	unordered_map<int,string> &columnMap, unordered_map<int,int> &columnTypeMap)
{
	// prepare columns
	for (size_t i=0; i<columns.size(); i++) {
		int dbfLoc = DBFGetFieldIndex(dbf,columns[i].c_str());
		if (dbfLoc>-1) { 
			columnMap[dbfLoc]=columns[i];
			columnTypeMap[dbfLoc]=DBFGetFieldInfo(dbf,dbfLoc,NULL,NULL,NULL);
		}
		else
			cout << "Cound not find field in shp: " << columns[i] << endl;
	}
}

void prepareShapefile(const string &filename,
		const vector<string> &columns,
		class ShapeFileResultsDecoder &outObj) 
{
	DBFHandle dbf = nullptr;
	try
	{
		dbf = DBFOpen(filename.c_str(), "rb");
		if(dbf == nullptr)
			return;

		unordered_map<int,string> columnMap;
		unordered_map<int,int> columnTypeMap;
		GetColumnMaps(dbf, columns, columnMap, columnTypeMap);

		setLayerColumnTypes(dbf, columnMap, columnTypeMap, outObj);
	}
	catch(exception &err)
	{
		DBFClose(dbf);
		throw err;
	}
	DBFClose(dbf);
}

void readShapeEntity(int i, SHPHandle shp, DBFHandle dbf, 
	const unordered_map<int,string> &columnMap, const unordered_map<int,int> &columnTypeMap,
	int indexField, bool clipBoxSet, const Box &clippingBox,
	class ShapeFileResultsDecoder &outObj)
{
	SHPObject* shape = SHPReadObject(shp, i);
	vector<Point> points;

	if(shape == nullptr)
	{
		cerr << "Error loading shape from shapefile" << endl;
		return;
	}

    // Check shape is in clippingBox
	Box shapeBox(Point(shape->dfXMin, lat2latp(shape->dfYMin)), Point(shape->dfXMax, lat2latp(shape->dfYMax)));
	if (clipBoxSet and (shapeBox.min_corner().get<0>() > clippingBox.max_corner().get<0>() or 
		shapeBox.max_corner().get<0>() < clippingBox.min_corner().get<0>() or 
		shapeBox.min_corner().get<1>() > clippingBox.max_corner().get<1>() or
		shapeBox.max_corner().get<1>() < clippingBox.min_corner().get<1>()))
	{
		SHPDestroyObject(shape);
		return;
	}

	int shapeType = shape->nSHPType;	// 1=point, 3=polyline, 5=(multi)polygon [8=multipoint, 11+=3D]

	if (shapeType==1) {
		// Points
		Point p( shape->padfX[0], lat2latp(shape->padfY[0]) );
		if (!clipBoxSet or geom::within(p, clippingBox)) {

			string name;
			bool hasName = false;
			if (indexField>-1) { name=DBFReadStringAttribute(dbf, i, indexField); hasName = true;}

			map<string, ShpFieldValue> keyVals;
			GetShapefileAttributes(dbf, i, columnMap, columnTypeMap, keyVals);

			outObj.AddObject(i, CACHED_POINT, p, hasName, name, keyVals);
		}

	} else if (shapeType==3) {
		// (Multi)-polylines
		// Due to https://svn.boost.org/trac/boost/ticket/11268, we can't clip a MultiLinestring with Boost 1.56-1.58, 
		// so we need to create everything as polylines and clip individually :(
		for (int j=0; j<shape->nParts; j++) {
			Linestring ls;
			fillPointArrayFromShapefile(&points, shape, j);
			geom::assign_points(ls, points);
			MultiLinestring out;
			if(clipBoxSet)
				geom::intersection(ls, clippingBox, out);
			else
				out.push_back(ls);
			for (MultiLinestring::const_iterator it = out.begin(); it != out.end(); ++it) {

				string name;
				bool hasName = false;
				if (indexField>-1) { name=DBFReadStringAttribute(dbf, i, indexField); hasName = true;}

				map<string, ShpFieldValue> keyVals;
				GetShapefileAttributes(dbf, i, columnMap, columnTypeMap, keyVals);

				outObj.AddObject(i, CACHED_LINESTRING, *it, hasName, name, keyVals);
			}
		}

	} else if (shapeType==5) {
		// (Multi)-polygons
		MultiPolygon multi;
		Polygon poly;
		Ring ring;
		int nInteriorRings = 0;

		// To avoid expensive computations, we assume the shapefile has been pre-processed
		// such that each polygon's exterior ring is immediately followed by its interior rings.
		for (int j=0; j<shape->nParts; j++) {
			fillPointArrayFromShapefile(&points, shape, j);
			// Read points into a ring
			ring.clear();
			geom::append(ring, points);

			if (j == 0) {
				// We assume the first part is an exterior ring of the first polygon.
				geom::append(poly, ring);
			}
			else if (geom::area(ring) > 0.0) {
				// This part has clockwise orientation - an exterior ring.
				// Start a new polygon.
				multi.push_back(poly);
				poly.clear();
				nInteriorRings = 0;
				geom::append(poly, ring);
			} else {
				// This part has anti-clockwise orientation.
				// Add another interior ring to the current polygon.
				nInteriorRings++;
				geom::interior_rings(poly).resize(nInteriorRings);
				geom::append(poly, ring, nInteriorRings - 1);
			}
		}
		// All parts read. Add the last polygon.
		multi.push_back(poly);

		string reason;
		if (!geom::is_valid(multi, reason)) {
			cerr << "Shapefile entity #" << i << " type " << shapeType << " is invalid. Parts:" << shape->nParts << ". Reason:" << reason;
			geom::correct(multi);
			geom::remove_spikes(multi);	// water polygon shapefile has many spikes
			if (geom::is_valid(multi, reason)) {
				cerr << "... corrected";
			} else {
				cerr << "... failed to correct. Reason: " << reason;
			}
			cerr << endl;
		}
		// clip to bounding box
		MultiPolygon out;
		if(clipBoxSet)
			geom::intersection(multi, clippingBox, out);
		else
			out = multi;
		if (boost::size(out)>0) {

			string name;
			bool hasName = false;
			if (indexField>-1) { name=DBFReadStringAttribute(dbf, i, indexField); hasName = true;}

			map<string, ShpFieldValue> keyVals;
			GetShapefileAttributes(dbf, i, columnMap, columnTypeMap, keyVals);

			// create OutputObject
			outObj.AddObject(i, CACHED_POLYGON, out, hasName, name, keyVals);
		}

	} else {
		// Not supported
		cerr << "Shapefile entity #" << i << " type " << shapeType << " not supported" << endl;
	}

	SHPDestroyObject(shape);
}

// *************************************************************

ShapefileReader::ShapefileReader(const std::string &filename, 					
					const std::vector<std::string> &columns,
					const std::string &indexName)
{
	indexField = -1;
	numEntities=0;
	shpType = -1;

	// open shapefile
	shp = SHPOpen(filename.c_str(), "rb");
	if(shp == nullptr)
		return;
	dbf = DBFOpen(filename.c_str(), "rb");
	if(dbf == nullptr)
		return;
	SHPGetInfo(shp, &numEntities, &shpType, adfMinBound, adfMaxBound);
	
	// prepare columns
	GetColumnMaps(dbf, columns, columnMap, columnTypeMap);

	if (indexName!="") { indexField = DBFGetFieldIndex(dbf,indexName.c_str()); }

}

ShapefileReader::~ShapefileReader()
{
	if(shp != nullptr)
		SHPClose(shp);
	if(dbf != nullptr)
		DBFClose(dbf);
}

void ShapefileReader::ReadAllInBox(const Box &clippingBox, class ShapeFileResultsDecoder &outObj) 
{
	for (int i=0; i<numEntities; i++) {
		readShapeEntity(i, shp, dbf, 
			columnMap, columnTypeMap,
			indexField, true, clippingBox, outObj);
	}
}

void ShapefileReader::ReadIndexInBox(int index, bool clipBoxSet, const Box &clippingBox, class ShapeFileResultsDecoder &outObj)
{
	readShapeEntity(index, shp, dbf, 
		columnMap, columnTypeMap,
		indexField, clipBoxSet, clippingBox, outObj);	
}

