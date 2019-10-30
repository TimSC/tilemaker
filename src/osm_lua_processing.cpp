#include "osm_lua_processing.h"
#include <iostream>
using namespace std;

// ----	initialization routines

OsmLuaProcessing::OsmLuaProcessing(const class Config &configIn, class LayerDefinition &layers,
	const string &luaFile,
	const class TileDataSource &shpMemTiles, 
	class TileDataSource &outputTiles):
	shpMemTiles(shpMemTiles),
	outputTiles(outputTiles),
	config(configIn),
	layers(layers),
	IDataStreamHandler()
{
	newWayID = MAX_WAY_ID;
	readPreprocessing = false;

	// ----	Initialise Lua
	luaState.setErrorHandler(kaguya::ErrorHandler::throwDefaultError);
	luaState.dofile(luaFile.c_str());
	luaState["OSM"].setClass(kaguya::UserdataMetatable<OsmLuaProcessing>()
		.addFunction("Id", &OsmLuaProcessing::Id)
		.addFunction("Holds", &OsmLuaProcessing::Holds)
		.addFunction("Find", &OsmLuaProcessing::Find)
		.addFunction("FindIntersecting", &OsmLuaProcessing::FindIntersecting)
		.addFunction("Intersects", &OsmLuaProcessing::Intersects)
		.addFunction("IsClosed", &OsmLuaProcessing::IsClosed)
		.addFunction("ScaleToMeter", &OsmLuaProcessing::ScaleToMeter)
		.addFunction("ScaleToKiloMeter", &OsmLuaProcessing::ScaleToKiloMeter)
		.addFunction("Area", &OsmLuaProcessing::Area)
		.addFunction("Length", &OsmLuaProcessing::Length)
		.addFunction("Layer", &OsmLuaProcessing::Layer)
		.addFunction("LayerAsCentroid", &OsmLuaProcessing::LayerAsCentroid)
		.addFunction("Attribute", &OsmLuaProcessing::Attribute)
		.addFunction("AttributeNumeric", &OsmLuaProcessing::AttributeNumeric)
		.addFunction("AttributeBoolean", &OsmLuaProcessing::AttributeBoolean)
	);

	// ---- Call init_function of Lua logic

	luaState("if init_function~=nil then init_function() end");

	vector<string> nodeKeyVec = luaState["node_keys"];
	this->significantKeys.insert(nodeKeyVec.begin(), nodeKeyVec.end());
}

OsmLuaProcessing::~OsmLuaProcessing()
{
	// Call exit_function of Lua logic
	luaState("if exit_function~=nil then exit_function() end");
}

// ----	Helpers provided for main routine

// Has this object been assigned to any layers?
bool OsmLuaProcessing::empty() {
	return outputs.size()==0;
}

// ----	Metadata queries called from Lua

// Get the ID of the current object
string OsmLuaProcessing::Id() const {
	return to_string(osmID);
}

// Check if there's a value for a given key
bool OsmLuaProcessing::Holds(const string& key) const {
	
	return currentTags.find(key) != currentTags.end();
}

// Get an OSM tag for a given key (or return empty string if none)
string OsmLuaProcessing::Find(const string& key) const {

	auto it = currentTags.find(key);
	if(it == currentTags.end()) return "";
	return it->second;
}

// ----	Spatial queries called from Lua

// Find intersecting shapefile layer
vector<string> OsmLuaProcessing::FindIntersecting(const string &layerName) 
{
	// TODO: multipolygon relations not supported, will always return empty vector
	if(isRelation) return vector<string>();
	Point p1(lon1/10000000.0,latp1/10000000.0);
	Point p2(lon2/10000000.0,latp2/10000000.0);
	Box box = Box(p1,p2);
	return shpMemTiles.FindIntersecting(layerName, box);
}

bool OsmLuaProcessing::Intersects(const string &layerName)
{
	// TODO: multipolygon relations not supported, will always return false
	if(isRelation) return false;
	Point p1(lon1/10000000.0,latp1/10000000.0);
	Point p2(lon2/10000000.0,latp2/10000000.0);
	Box box = Box(p1,p2);
	return shpMemTiles.Intersects(layerName, box);
}

// Returns whether it is closed polygon
bool OsmLuaProcessing::IsClosed() const {
	if (!isWay) return false; // nonsense: it isn't a way
	if (isRelation) {
		return true; // TODO: check it when non-multipolygon are supported
	} else {
		return nodeVec.front() == nodeVec.back();
	}
}

// Scale to (kilo)meter
double OsmLuaProcessing::ScaleToMeter() {
	return degp2meter(1.0, (latp1/2+latp2/2)/10000000.0);
}

double OsmLuaProcessing::ScaleToKiloMeter() {
	return (1/1000.0) * ScaleToMeter();
}

// Returns area
double OsmLuaProcessing::Area() {
	if (!IsClosed()) return 0;
	if (isRelation) {
		return geom::area(multiPolygonCached());
	} else if (isWay) {
		return geom::area(polygonCached());
	}
	return 0;
}

// Returns length
double OsmLuaProcessing::Length() {
	if (isRelation) {
		return geom::length(multiPolygonCached());
	} else if (isWay) {
		return geom::length(linestringCached());
	}
	return 0;
}

// Cached geometries creation
const Linestring &OsmLuaProcessing::linestringCached() {
	if (!linestringInited) {
		linestringInited = true;

		if(isRelation)
		{
			//A relation is being treated as a linestring, which might be
			//caused by bug in the Lua script
			linestringCache = osmStore.wayListLinestring(*outerWayVec, *innerWayVec);
		}
		else if(isWay)
		{
			linestringCache = osmStore.nodeListLinestring(nodeVec);
		}
	}
	return linestringCache;
}

const Polygon &OsmLuaProcessing::polygonCached() {
	if (!polygonInited) {
		polygonInited = true;
		polygonCache = osmStore.nodeListPolygon(nodeVec);
	}
	return polygonCache;
}

const MultiPolygon &OsmLuaProcessing::multiPolygonCached() {
	if (!multiPolygonInited) {
		multiPolygonInited = true;
		multiPolygonCache = osmStore.wayListMultiPolygon(*outerWayVec, *innerWayVec);
	}
	return multiPolygonCache;
}

// ----	Requests from Lua to write this way/node to a vector tile's Layer

// Add object to specified layer from Lua
void OsmLuaProcessing::Layer(const string &layerName, bool area) {
	if (layers.layerMap.count(layerName) == 0) {
		throw out_of_range("ERROR: Layer(): a layer named as \"" + layerName + "\" doesn't exist.");
	}

	Geometry geomOut;
	OutputGeometryType geomType = isWay ? (area ? POLYGON : LINESTRING) : POINT;
	try {
		if (geomType==POINT) {
			LatpLon pt = osmStore.nodes.at(osmID);
			geomOut = Point(pt.lon, pt.latp);
		}
		else if (geomType==POLYGON) {
			// polygon

			if(isRelation)
			{
				try
				{
					geomOut = multiPolygonCached();
				}
				catch(std::out_of_range &err)
				{
					cout << "In relation " << osmID << ": " << err.what() << endl;
					return;
				}
			}
			else if(isWay)
			{
				//Is there a more efficient way to do this?
				Linestring ls = linestringCached();
				Polygon p;
				geom::assign_points(p, ls);
				MultiPolygon mp;
				mp.push_back(p);
				geomOut = mp;
			}

		}
		else if (geomType==LINESTRING) {
			// linestring
			geomOut = linestringCached();
		}
	} catch (std::invalid_argument &err) {
		cerr << "Error in OutputObjectOsmStore constructor: " << err.what() << endl;
	}

	OutputObjectRef oo = std::make_shared<OutputObjectOsmStore>(geomType,
					layers.layerMap[layerName],
					osmID, geomOut);
	outputs.push_back(oo);
}

void OsmLuaProcessing::LayerAsCentroid(const string &layerName) {
	if (layers.layerMap.count(layerName) == 0) {
		throw out_of_range("ERROR: LayerAsCentroid(): a layer named as \"" + layerName + "\" doesn't exist.");
	}

	Geometry centeroid;
	try {

		Geometry tmp;
		if(isRelation)
		{
			try
			{
				tmp = osmStore.wayListMultiPolygon(*outerWayVec, *innerWayVec);
			}
			catch(std::out_of_range &err)
			{
				cout << "In relation " << osmID << ": " << err.what() << endl;
				return;
			}
		}
		else if(isWay)
		{
			//Is there a more efficient way to do this?
			Linestring ls = linestringCached();
			Polygon p;
			geom::assign_points(p, ls);
			MultiPolygon mp;
			mp.push_back(p);	
			tmp = mp;
		}

		if(geom::is_empty(tmp))
		{
			cerr << "Geometry is empty in OsmLuaProcessing::LayerAsCentroid" << endl;
			return;
		}

		// write out centroid only
		try
		{
			Point p;
			geom::centroid(tmp, p);
			centeroid = p;
		}
		catch (geom::centroid_exception &err)
		{
			cerr << "Problem geom: " << boost::geometry::wkt(tmp) << std::endl;
			cerr << err.what() << endl;
			return;
		}

	} catch (std::invalid_argument &err) {
		cerr << "Error in OutputObjectOsmStore constructor: " << err.what() << endl;
	}

	OutputObjectRef oo = std::make_shared<OutputObjectOsmStore>(CENTROID,
					layers.layerMap[layerName],
					osmID, centeroid);
	outputs.push_back(oo);
}

// Set attributes in a vector tile's Attributes table
void OsmLuaProcessing::Attribute(const string &key, const string &val) {
	if (val.size()==0) { return; }		// don't set empty strings
	if (outputs.size()==0) { cerr << "Can't add Attribute " << key << " if no Layer set" << endl; return; }
	vector_tile::Tile_Value v;
	v.set_string_value(val);
	outputs[outputs.size()-1]->addAttribute(key, v);
	setVectorLayerMetadata(outputs[outputs.size()-1]->layer, key, 0);
}

void OsmLuaProcessing::AttributeNumeric(const string &key, const float val) {
	if (outputs.size()==0) { cerr << "Can't add Attribute " << key << " if no Layer set" << endl; return; }
	vector_tile::Tile_Value v;
	v.set_float_value(val);
	outputs[outputs.size()-1]->addAttribute(key, v);
	setVectorLayerMetadata(outputs[outputs.size()-1]->layer, key, 1);
}

void OsmLuaProcessing::AttributeBoolean(const string &key, const bool val) {
	if (outputs.size()==0) { cerr << "Can't add Attribute " << key << " if no Layer set" << endl; return; }
	vector_tile::Tile_Value v;
	v.set_bool_value(val);
	outputs[outputs.size()-1]->addAttribute(key, v);
	setVectorLayerMetadata(outputs[outputs.size()-1]->layer, key, 2);
}

// Record attribute name/type for vector_layers table
void OsmLuaProcessing::setVectorLayerMetadata(const uint_least8_t layer, const string &key, const uint type) {
	layers.layers[layer].attributeMap[key] = type;
}

void OsmLuaProcessing::startOsmData()
{
	osmStore.clear();
}

bool OsmLuaProcessing::Finish() 
{
	return false;
}

bool OsmLuaProcessing::StoreIsDiff(bool) 
{
	return false;
}

bool OsmLuaProcessing::StoreBounds(double x1, double y1, double x2, double y2) 
{
	return false;
}

bool OsmLuaProcessing::StoreNode(int64_t objId, const class MetaData &metaData, 
	const TagMap &tags, double lat, double lon) 
{	
	if(readPreprocessing) return false;

	struct LatpLon nodePos;
	nodePos.latp = (int32_t)round(lat2latp(lat) * 10000000.0);
	nodePos.lon = (int32_t)round(lon * 10000000.0);

	osmStore.nodes.insert_back(objId, nodePos);

	if(tags.size() > 0 && CheckTagsIfSignificant(tags))
	{
		reset();
		osmID = objId;
		isWay = false;
		isRelation = false;

		setLocation(nodePos.lon, nodePos.latp, nodePos.lon, nodePos.latp);

		currentTags = tags;

		//Start Lua processing for node
		try
		{
			luaState["node_function"](this);
		}
		catch(kaguya::LuaRuntimeError &err)
		{
			cerr << "kaguya::LuaRuntimeError: " << err.what() << endl;
			auto debug = luaState["debug"];
			auto traceback = debug["traceback"];
			cerr << traceback() << endl;
			return false;
		}

		if (!this->empty()) {
			TileCoordinates index = latpLon2index(nodePos, this->outputTiles.GetBaseZoom());
			for (auto jt = this->outputs.begin(); jt != this->outputs.end(); ++jt) {
				outputTiles.AddObject(index, *jt);
			}
		}
	}

	return false;
}

bool OsmLuaProcessing::StoreWay(int64_t objId, const class MetaData &metaData, 
	const TagMap &tags, const std::vector<int64_t> &refs) 
{
	if(readPreprocessing) return false;

	reset();
	osmID = objId;
	isWay = true;
	isRelation = false;
	bool inRelation = (waysInRelation.find(objId) != waysInRelation.end());

	outerWayVec = nullptr;
	innerWayVec = nullptr;
	nodeVec.clear();
	nodeVec.insert(nodeVec.end(), refs.begin(), refs.end());
	try {
		setLocation(osmStore.nodes.at(nodeVec.front()).lon, osmStore.nodes.at(nodeVec.front()).latp,
				osmStore.nodes.at(nodeVec.back()).lon, osmStore.nodes.at(nodeVec.back()).latp);

	} catch (std::out_of_range &err) {
		std::stringstream ss;
		ss << "Way " << osmID << " is missing a node, " << err.what();
		throw std::out_of_range(ss.str());
	}

	currentTags = tags;

	bool ok = true;
	if (ok)
	{
		//Start Lua processing for way
		try
		{
			kaguya::LuaFunction way_function = luaState["way_function"];
			kaguya::LuaRef ret = way_function(this);
		}
		catch(kaguya::LuaRuntimeError &err)
		{
			cerr << "kaguya::LuaRuntimeError: " << err.what() << endl;
			auto debug = luaState["debug"];
			auto traceback = debug["traceback"];
			cerr << traceback() << endl;
			return false;
		}
	}

	if (!this->empty() || inRelation) {
		// Store the way's nodes in the global way store
		WayStore &ways = osmStore.ways;
		WayID wayId = static_cast<WayID>(objId);
		ways.insert_back(wayId, nodeVec);
	}

	if (!this->empty()) {
		// create a list of tiles this way passes through (tileSet)
		unordered_set<TileCoordinates> tileSet;
		try {
			insertIntermediateTiles(osmStore.nodeListLinestring(nodeVec), this->outputTiles.GetBaseZoom(), tileSet);

			// then, for each tile, store the OutputObject for each layer
			bool polygonExists = false;
			for (auto it = tileSet.begin(); it != tileSet.end(); ++it) {
				TileCoordinates index = *it;
				for (auto jt = this->outputs.begin(); jt != this->outputs.end(); ++jt) {
					if ((*jt)->geomType == POLYGON) {
						polygonExists = true;
						continue;
					}
					outputTiles.AddObject(index, *jt);
				}
			}

			// for polygon, fill inner tiles
			if (polygonExists) {
				fillCoveredTiles(tileSet);
				for (auto it = tileSet.begin(); it != tileSet.end(); ++it) {
					TileCoordinates index = *it;
					for (auto jt = this->outputs.begin(); jt != this->outputs.end(); ++jt) {
						if ((*jt)->geomType != POLYGON) continue;
						outputTiles.AddObject(index, *jt);
					}
				}
			}
		} catch(std::out_of_range &err)
		{
			cerr << "Error calculating intermediate tiles: " << err.what() << endl;
		}
	}

	return false;
}

bool OsmLuaProcessing::StoreRelation(int64_t objId, const class MetaData &metaData, const TagMap &tags, 
	const std::vector<std::string> &refTypeStrs, const std::vector<int64_t> &refIds, 
	const std::vector<std::string> &refRoles) 
{
	if(readPreprocessing)
	{
		for(size_t i=0; i<refIds.size(); i++)
			if(refTypeStrs[i] == "way")
				waysInRelation.insert(refIds[i]);
		return false;
	}

	reset();
	osmID = --newWayID;
	isWay = true;
	isRelation = true;

	WayVec outerWayVec;
	WayVec innerWayVec;
	nodeVec.clear();

	auto it = tags.find("type");
	if(it == tags.end() or it->second != "multipolygon") return false; //Only multipolygons are supported

	for(size_t i=0; i<refIds.size(); i++)
	{
		if(refTypeStrs[i] != "way") continue;

		if(refRoles[i] == "outer") outerWayVec.push_back(refIds[i]);
		else if(refRoles[i] == "inner") innerWayVec.push_back(refIds[i]);
	}
	this->outerWayVec = &outerWayVec;
	this->innerWayVec = &innerWayVec;
	
	//setLocation(...); TODO
	currentTags = tags;

	bool ok = true;
	if (ok)
	{
		try
		{
			//Start Lua processing for relation
			luaState["way_function"](this);
		}
		catch(kaguya::LuaRuntimeError &err)
		{
			cerr << "kaguya::LuaRuntimeError: " << err.what() << endl;
			auto debug = luaState["debug"];
			auto traceback = debug["traceback"];
			cerr << traceback() << endl;
			return false;
		}
	}

	if (!this->empty()) {								

		WayID relID = this->osmID;
		// Store the relation members in the global relation store
		RelationStore &relations = osmStore.relations;
		relations.insert_front(relID, outerWayVec, innerWayVec);

		MultiPolygon mp;
		try
		{
			// for each tile the relation may cover, put the output objects.
			mp = osmStore.wayListMultiPolygon(outerWayVec, innerWayVec);
		}
		catch(std::out_of_range &err)
		{
			cout << "In relation " << relID << ": " << err.what() << endl;
			return false;
		}		

		unordered_set<TileCoordinates> tileSet;
		if (mp.size() == 1) {
			insertIntermediateTiles(mp[0].outer(), this->outputTiles.GetBaseZoom(), tileSet);
			fillCoveredTiles(tileSet);
		} else {
			for (Polygon poly: mp) {
				unordered_set<TileCoordinates> tileSetTmp;
				insertIntermediateTiles(poly.outer(), this->outputTiles.GetBaseZoom(), tileSetTmp);
				fillCoveredTiles(tileSetTmp);
				tileSet.insert(tileSetTmp.begin(), tileSetTmp.end());
			}
		}

		for (auto it = tileSet.begin(); it != tileSet.end(); ++it) {
			TileCoordinates index = *it;
			for (auto jt = this->outputs.begin(); jt != this->outputs.end(); ++jt) {
				outputTiles.AddObject(index, *jt);
			}
		}
	}

	return false;
}

bool OsmLuaProcessing::CheckTagsIfSignificant(const TagMap &tags)
{
	for(auto it=tags.begin(); it != tags.end(); it ++)
	{
		auto it2 = this->significantKeys.find(it->first);
		if(it2 != this->significantKeys.end()) return true;
	}	

	return false;
}

