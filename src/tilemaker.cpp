/*! \file */ 

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include <sstream>
#include <chrono>

#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/error/en.h"

#ifndef _MSC_VER
#include <sys/resource.h>
#endif

#include "geom.h"
#include "config_validator.h"
#include "helpers.h"
#include "coordinates.h"
#include "coordinates_geom.h"
#include "significant_tags.h"
#include "attribute_store.h"
#include "output_object.h"
#include "osm_lua_processing.h"
#include "options_parser.h"
#include "shared_data.h"
#include "osm_area.h"
#include "osm_area_processor.h"
#include "geojson_processor.h"
#include "shp_processor.h"
#include "tile_worker.h"
#include "osm_mem_tiles.h"
#include "shp_mem_tiles.h"

// Namespaces
using namespace std;
namespace geom = boost::geometry;

// Global verbose switch
bool verbose = false;

// The lowest zoom rendered. An area's data is only enough for tiles inside it,
// and lower zooms would need far larger areas than the input is meant to hold.
const unsigned int MIN_ZOOM = 11;

/**
 *\brief The Main function is responsible for command line processing, loading data and writing the tiles.
 *
 * Data is loaded into OsmMemTiles and ShpMemTiles.
 *
 * Each tile is then written by outputProc.
 */
int main(const int argc, const char* argv[]) {
	// ----	Read command-line options
	OptionsParser::Options options;
	try {
		options = OptionsParser::parse(argc, argv);
	} catch (OptionsParser::OptionException& e) {
		cerr << e.what() << endl;
		return 1;
	}

	if (options.showHelp) { OptionsParser::showHelp(); return 0; }

	if (options.quiet) {
		// Suppress anything written to std out
		std::cout.setstate(std::ios_base::failbit);
	}

	verbose = options.verbose;

	// One tile: read it and a margin around it, for what crosses its edges
	// (labels, and the buffer tiles are drawn with)
	if (options.hasTile) {
		if (options.tileZoom < MIN_ZOOM) {
			cerr << "Tiles below z" << MIN_ZOOM << " are not rendered" << endl;
			return -1;
		}
		const uint8_t z = options.tileZoom;
		double west = tilex2lon(options.tileX, z), east = tilex2lon(options.tileX + 1, z);
		double north = tiley2lat(options.tileY, z), south = tiley2lat(options.tileY + 1, z);
		double marginLon = (east - west) / 8, marginLat = (north - south) / 8;
		ostringstream box;
		box.precision(10);
		box << west - marginLon << "," << south - marginLat << "," << east + marginLon << "," << north + marginLat;
		options.bbox = box.str();
	}

	vector<string> bboxElements = parseBox(options.bbox);

	// ---- Remove existing .mbtiles if it exists
	if ((options.outputMode == OptionsParser::OutputMode::MBTiles || options.outputMode == OptionsParser::OutputMode::PMTiles) && static_cast<bool>(std::ifstream(options.outputFile))) {
		cout << "Output file exists, overwriting" << endl;
		if (remove(options.outputFile.c_str()) != 0) {
			cerr << "Couldn't remove existing file" << endl;
			return -1;
		}
	}


	// ----	Read the OSM data of the area

	OsmArea osmArea;
	for (const auto& inputFile : options.inputFiles) {
		cout << "Reading " << inputFile << endl;
		try {
			ReadOsmFile(inputFile, osmArea);
		} catch (const std::exception& e) {
			cerr << e.what() << endl;
			return -1;
		}
	}
	if (options.useDatabase) {
		vector<string> box = parseBox(options.bbox);
		cout << "Reading the map database" << endl;
		try {
			ReadOsmDatabase(options.dbConnection, options.tablePrefix, options.modifyPrefix,
				bboxElementFromStr(box.at(0)), bboxElementFromStr(box.at(1)),
				bboxElementFromStr(box.at(2)), bboxElementFromStr(box.at(3)), osmArea);
		} catch (const std::exception& e) {
			cerr << e.what() << endl;
			return -1;
		}
	}
	cout << "Read " << osmArea.nodes.size() << " nodes, " << osmArea.ways.size() << " ways, " << osmArea.relations.size() << " relations" << endl;

	// ----	The area's bounds: --bbox, or those the input gives, or those of its nodes

	bool hasClippingBox = false;
	Box clippingBox;
	double minLon = 0.0, maxLon = 0.0, minLat = 0.0, maxLat = 0.0;
	if (!bboxElements.empty()) {
		hasClippingBox = true;
		minLon = bboxElementFromStr(bboxElements.at(0));
		minLat = bboxElementFromStr(bboxElements.at(1));
		maxLon = bboxElementFromStr(bboxElements.at(2));
		maxLat = bboxElementFromStr(bboxElements.at(3));
	} else if (osmArea.hasBounds) {
		hasClippingBox = true;
		minLon = osmArea.minLon;
		minLat = osmArea.minLat;
		maxLon = osmArea.maxLon;
		maxLat = osmArea.maxLat;
	} else if (!osmArea.nodes.empty()) {
		hasClippingBox = true;
		minLon = maxLon = osmArea.nodes.front().lon;
		minLat = maxLat = osmArea.nodes.front().lat;
		for (const auto& node : osmArea.nodes) {
			minLon = std::min(minLon, node.lon);
			maxLon = std::max(maxLon, node.lon);
			minLat = std::min(minLat, node.lat);
			maxLat = std::max(maxLat, node.lat);
		}
	}

	if (hasClippingBox) {
		clippingBox = Box(geom::make<Point>(minLon, lat2latp(minLat)),
		                  geom::make<Point>(maxLon, lat2latp(maxLat)));
	}

	// ----	Read JSON config

	rapidjson::Document jsonConfig;
	class Config config;
	try {
		FilePtr fp = openFile(options.jsonFile, "r");
		char readBuffer[65536];
		rapidjson::FileReadStream is(fp.get(), readBuffer, sizeof(readBuffer));
		jsonConfig.ParseStream(is);
		if (jsonConfig.HasParseError()) {
			cerr << "Invalid JSON file: " << rapidjson::GetParseError_En(jsonConfig.GetParseError())
			     << " at offset " << jsonConfig.GetErrorOffset() << "." << endl;
			return -1;
		}

		string jsonError;
		if (!validateConfigJson(jsonConfig, jsonError)) {
			cerr << "Invalid JSON file: " << jsonError << "." << endl;
			return -1;
		}

		config.readConfig(jsonConfig, hasClippingBox, clippingBox);
	} catch (...) {
		cerr << "Couldn't find expected details in JSON file." << endl;
		return -1;
	}
	if (config.endZoom < MIN_ZOOM) {
		cerr << "maxzoom must be at least " << MIN_ZOOM << endl;
		return -1;
	}
	if (options.hasTile) {
		if (options.tileZoom > config.endZoom) {
			cerr << "The config's maxzoom is " << config.endZoom << endl;
			return -1;
		}
		config.startZoom = config.endZoom = options.tileZoom;
	} else if (config.startZoom < MIN_ZOOM) {
		cout << "Rendering from z" << MIN_ZOOM << " (config minzoom is " << config.startZoom << ")" << endl;
		config.startZoom = MIN_ZOOM;
	}
	if (hasClippingBox) {
		cout << "Bounding box " << clippingBox.min_corner().x() << ", " << latp2lat(clippingBox.min_corner().y()) << ", " << 
		                           clippingBox.max_corner().x() << ", " << latp2lat(clippingBox.max_corner().y()) << endl;
	}

	OSMStore osmStore;
	osmStore.enforce_integrity(!options.osm.skipIntegrity);

	AttributeStore attributeStore;

	class LayerDefinition layers(config.layers);

	class Declutter declutter;
	declutter.configure(layers.layers);

	const unsigned int indexZoom = std::min(config.baseZoom, 14u);
	class OsmMemTiles osmMemTiles(indexZoom, config.includeID);
	class ShpMemTiles shpMemTiles(indexZoom, declutter);
	osmMemTiles.open();
	shpMemTiles.open();

	OsmLuaProcessing osmLuaProcessing(osmStore, config, layers, options.luaFile, 
		shpMemTiles, osmMemTiles, attributeStore, declutter);

	// ---- Load external sources (shp/geojson)

	{
		ShpProcessor shpProcessor(clippingBox, shpMemTiles, osmLuaProcessing);
		GeoJSONProcessor geoJSONProcessor(clippingBox, shpMemTiles, osmLuaProcessing);
		for (size_t layerNum=0; layerNum<layers.layers.size(); layerNum++) {
			LayerDef &layer = layers.layers[layerNum];
			if(layer.indexed) { shpMemTiles.CreateNamedLayerIndex(layer.name); }

			// A layer only shown below the zooms rendered is not read, unless the
			// Lua profile can query it
			if (layer.source.size()>0 && layer.maxzoom < config.startZoom && !layer.indexed) {
				if (verbose) cout << "Skipping " << layer.name << ", only shown below z" << config.startZoom << endl;
				continue;
			}

			if (layer.source.size()>0) {
				if (!hasClippingBox) {
					cerr << "Can't read shapefiles unless a bounding box is provided." << endl;
					exit(EXIT_FAILURE);
				} else {
					try {
						if (ends_with(layer.source, "json") || ends_with(layer.source, "jsonl") || ends_with(layer.source, "JSON") || ends_with(layer.source, "JSONL") || ends_with(layer.source, "jsonseq") || ends_with(layer.source, "JSONSEQ")) {
							cout << "Reading GeoJSON " << layer.name << endl;
							geoJSONProcessor.read(layers.layers[layerNum], layerNum);
						} else {
							cout << "Reading shapefile " << layer.name << endl;
							shpProcessor.read(layers.layers[layerNum], layerNum);
						}
					} catch (const std::exception& e) {
						cerr << "Error reading external source " << layer.source << ": " << e.what() << endl;
						return -1;
					} catch (...) {
						cerr << "Unknown error reading external source " << layer.source << endl;
						return -1;
					}
				}
			}
		}
	}
	shpMemTiles.reportSize();

	// ----	Read significant node/way tags
	const SignificantTags significantNodeTags = osmLuaProcessing.GetSignificantNodeKeys();
	const SignificantTags significantWayTags = osmLuaProcessing.GetSignificantWayKeys();

	// ----	Run the OSM data through the Lua profile

	ProcessOsmArea(osmArea, osmStore, osmLuaProcessing, significantNodeTags, significantWayTags);
	osmArea = OsmArea(); // no longer needed
	osmStore.reportSize();
	std::vector<bool> sortOrders = layers.getSortOrders();
	osmMemTiles.reportSize();
	attributeStore.reportSize();
	osmLuaProcessing.dataStore.clear(); // no longer needed

	// ----	Initialise SharedData

	SourceList sources = {&osmMemTiles, &shpMemTiles};
	class SharedData sharedData(config, layers);
	sharedData.outputFile = options.outputFile;
	sharedData.outputMode = options.outputMode;

	// ----	Initialise mbtiles/pmtiles if required
	
	if (sharedData.outputMode == OptionsParser::OutputMode::MBTiles) {
		sharedData.mbtiles.openForWriting(sharedData.outputFile);
		sharedData.writeMBTilesProjectData();
	} else if (sharedData.outputMode == OptionsParser::OutputMode::PMTiles) {
		sharedData.pmtiles.open(sharedData.outputFile);
	}

	// ----	Write out data

	// Rank any decluttered point features, and index them at their allotted zoom levels
	declutter.apply(layers.layers, osmMemTiles, shpMemTiles);

	// The tiles over the area, or the one tile asked for, from z11 up; those
	// with nothing in them are skipped
	uint64_t tilesWritten = 0;
	for (uint zoom = sharedData.config.startZoom; zoom <= sharedData.config.endZoom; zoom++) {
		uint32_t minX = 0, maxX = (1u << zoom) - 1, minY = 0, maxY = (1u << zoom) - 1;
		if (options.hasTile) {
			minX = maxX = options.tileX;
			minY = maxY = options.tileY;
		} else if (hasClippingBox) {
			minX = lon2tilex(clippingBox.min_corner().x(), zoom);
			maxX = std::min(maxX, lon2tilex(clippingBox.max_corner().x(), zoom));
			minY = latp2tiley(clippingBox.max_corner().y(), zoom);
			maxY = std::min(maxY, latp2tiley(clippingBox.min_corner().y(), zoom));
		}

		for (uint32_t x = minX; x <= maxX; x++) {
			for (uint32_t y = minY; y <= maxY; y++) {
				TileCoordinates coords(x, y);
				if (hasClippingBox && !options.hasTile && !boost::geometry::intersects(TileBbox(coords, zoom, false, false).getTileBox(), clippingBox))
					continue;

				auto start = std::chrono::steady_clock::now();
				std::vector<std::vector<OutputObjectID>> data;
				bool empty = true;
				for (auto source : sources) {
					data.emplace_back(source->getObjectsForTile(sortOrders, zoom, coords));
					empty = empty && data.back().empty();
				}
				if (empty) continue;

				outputProc(sharedData, sources, attributeStore, data, coords, zoom);
				tilesWritten++;

				if (options.logTileTimings) {
					double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
					cout << "z" << zoom << "/" << x << "/" << y << " took " << ms << " ms" << endl;
				} else if (ISATTY) {
					cout << "z" << zoom << "/" << x << "/" << y << ", " << tilesWritten << " tiles written               \r" << std::flush;
				}
			}
		}
	}
	cout << endl << tilesWritten << " tiles written" << endl;

	// ----	Close tileset

	if (options.outputMode == OptionsParser::OutputMode::MBTiles) {
		sharedData.writeMBTilesMetadata(jsonConfig);
		sharedData.mbtiles.closeForWriting();
	} else if (options.outputMode == OptionsParser::OutputMode::PMTiles) {
		sharedData.writePMTilesBounds();
		std::string metadata = sharedData.pmTilesMetadata(jsonConfig);
		sharedData.pmtiles.close(metadata);
	} else {
		sharedData.writeFileMetadata(jsonConfig);
	}

#ifndef _MSC_VER
	if (verbose) {
		struct rusage r_usage;
		getrusage(RUSAGE_SELF, &r_usage);
		cout << "\nMemory used: " << r_usage.ru_maxrss << endl;
	}
#endif

	cout << endl << "Filled the tileset with good things at " << sharedData.outputFile << endl;
}
