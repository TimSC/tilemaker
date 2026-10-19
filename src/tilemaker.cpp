/*! \file */ 

// C++ includes
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <sstream>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <chrono>

// Other utilities
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/variant.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio/thread_pool.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/error/en.h"

#ifndef _MSC_VER
#include <sys/resource.h>
#endif

#include "geom.h"

// Tilemaker code
#include "config_validator.h"
#include "helpers.h"
#include "coordinates.h"
#include "coordinates_geom.h"
#include "significant_tags.h"

#include "attribute_store.h"
#include "output_object.h"
#include "osm_lua_processing.h"
#include "mbtiles.h"

#include "options_parser.h"
#include "shared_data.h"
#include "osm_area.h"
#include "osm_area_processor.h"
#include "geojson_processor.h"
#include "shp_processor.h"
#include "tile_worker.h"
#include "osm_mem_tiles.h"
#include "shp_mem_tiles.h"

#include <boost/asio/post.hpp>
#include <boost/interprocess/streams/bufferstream.hpp>

#ifndef TM_VERSION
#define TM_VERSION (version not set)
#endif
#define STR1(x)  #x
#define STR(x)  STR1(x)

// Namespaces
using namespace std;
namespace po = boost::program_options;
namespace geom = boost::geometry;

// Global verbose switch
bool verbose = false;

// The lowest zoom rendered. An area's data is only enough for tiles inside it,
// and lower zooms would need far larger areas than the input is meant to hold.
const unsigned int MIN_ZOOM = 11;

void sortTileCoordinates(
    const size_t baseZoom,
    const size_t threadNum,
    std::deque<std::pair<unsigned int, TileCoordinates>>& tileCoordinates
);

/**
 *\brief The Main function is responsible for command line processing, loading data and starting worker threads.
 *
 * Data is loaded into OsmMemTiles and ShpMemTiles.
 *
 * Worker threads write the output tiles, and start in the outputProc function.
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
	if ((options.outputMode == OptionsParser::OutputMode::MBTiles || options.outputMode == OptionsParser::OutputMode::PMTiles) && !options.mergeSqlite && static_cast<bool>(std::ifstream(options.outputFile))) {
		cout << "Output file exists, overwriting";
		if (options.outputMode == OptionsParser::OutputMode::MBTiles) cout << " (use --merge to keep it)";
		cout << endl;
		if (remove(options.outputFile.c_str()) != 0) {
			cerr << "Couldn't remove existing file" << endl;
			return 0;
		}
	} else if (options.mergeSqlite && options.outputMode != OptionsParser::OutputMode::MBTiles) {
		cerr << "--merge only works with .mbtiles" << endl;
		return 0;
	} else if (options.mergeSqlite && !static_cast<bool>(std::ifstream(options.outputFile))) {
		cout << "--merge specified but .mbtiles file doesn't already exist, ignoring" << endl;
		options.mergeSqlite = false;
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
			ReadOsmDatabase(options.dbConnection, options.tablePrefix,
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
	class OsmMemTiles osmMemTiles(options.threadNum, indexZoom, config.includeID);
	class ShpMemTiles shpMemTiles(options.threadNum, indexZoom, declutter);
	osmMemTiles.open();
	shpMemTiles.open();

	OsmLuaProcessing osmLuaProcessing(osmStore, config, layers, options.luaFile, 
		shpMemTiles, osmMemTiles, attributeStore, declutter);

	// ---- Load external sources (shp/geojson)

	{
		ShpProcessor shpProcessor(clippingBox, options.threadNum, shpMemTiles, osmLuaProcessing);
		GeoJSONProcessor geoJSONProcessor(clippingBox, options.threadNum, shpMemTiles, osmLuaProcessing);
		for (size_t layerNum=0; layerNum<layers.layers.size(); layerNum++) {
			LayerDef &layer = layers.layers[layerNum];
			if(layer.indexed) { shpMemTiles.CreateNamedLayerIndex(layer.name); }

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
	attributeStore.finalize();
	osmMemTiles.reportSize();
	attributeStore.reportSize();
	osmLuaProcessing.dataStore.clear(); // no longer needed

	// ----	Initialise SharedData

	SourceList sources = {&osmMemTiles, &shpMemTiles};
	class SharedData sharedData(config, layers);
	sharedData.outputFile = options.outputFile;
	sharedData.outputMode = options.outputMode;
	sharedData.mergeSqlite = options.mergeSqlite;

	// ----	Initialise mbtiles/pmtiles if required
	
	if (sharedData.outputMode == OptionsParser::OutputMode::MBTiles) {
		sharedData.mbtiles.openForWriting(sharedData.outputFile);
		sharedData.writeMBTilesProjectData();
	} else if (sharedData.outputMode == OptionsParser::OutputMode::PMTiles) {
		sharedData.pmtiles.open(sharedData.outputFile);
	}

	// ----	Write out data

	// Launch the pool with threadNum threads
	boost::asio::thread_pool pool(options.threadNum);

	// Mutex is hold when IO is performed
	std::mutex io_mutex;

	// Loop through tiles
	std::atomic<uint64_t> tilesWritten(0), lastTilesWritten(0);

	// Rank any decluttered point features, and index them at their allotted zoom levels
	declutter.apply(layers.layers, osmMemTiles, shpMemTiles);

	for (auto source : sources) {
		source->finalize(options.threadNum);
	}
	// tiles by zoom level

	std::deque<std::pair<unsigned int, TileCoordinates>> tileCoordinates;
	std::vector<std::shared_ptr<TileCoordinatesSet>> zoomResults;
	zoomResults.reserve(sharedData.config.endZoom + 1);

	// Add PreciseTileCoordinatesSet, but only up to z14.
	for (uint zoom = 0; zoom <= std::min(14u, sharedData.config.endZoom); zoom++) {
		zoomResults.emplace_back(std::make_shared<PreciseTileCoordinatesSet>(zoom));
	}

	{
#ifdef CLOCK_MONOTONIC
		timespec start, end;
		clock_gettime(CLOCK_MONOTONIC, &start);
#endif
		std::cout << "collecting tiles" << std::flush;
		populateTilesAtZoom(sources, zoomResults);
#ifdef CLOCK_MONOTONIC
		clock_gettime(CLOCK_MONOTONIC, &end);
		uint64_t tileNs = 1e9 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
		std::cout << ": " << (uint32_t)(tileNs / 1e6) << "ms";
#endif
	}

	// Add LossyTileCoordinatesSet, if needed
	for (uint zoom = 15u; zoom <= sharedData.config.endZoom; zoom++) {
		zoomResults.emplace_back(std::make_shared<LossyTileCoordinatesSet>(zoom, *zoomResults[14]));
	}

	std::cout << ", filtering tiles:" << std::flush;
	for (uint zoom=sharedData.config.startZoom; zoom <= sharedData.config.endZoom; zoom++) {
		std::cout << " z" << std::to_string(zoom) << std::flush;
#ifdef CLOCK_MONOTONIC
		timespec start, end;
		clock_gettime(CLOCK_MONOTONIC, &start);
#endif

		const auto& zoomResult = zoomResults[zoom];
		int numTiles = 0;
		// Only the tiles over the area, or the one tile asked for
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
				if (!zoomResult->test(x, y))
					continue;
				if (hasClippingBox && !boost::geometry::intersects(TileBbox(TileCoordinates(x, y), zoom, false, false).getTileBox(), clippingBox))
					continue;

				tileCoordinates.push_back(std::make_pair(zoom, TileCoordinates(x, y)));
				numTiles++;
			}
		}

		std::cout << " (" << numTiles;
#ifdef CLOCK_MONOTONIC
		clock_gettime(CLOCK_MONOTONIC, &end);
		uint64_t tileNs = 1e9 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
		std::cout << ", " << (uint32_t)(tileNs / 1e6) << "ms";

#endif
		std::cout << ")" << std::flush;
	}
	zoomResults.clear();

	std::cout << std::endl;

	// Cluster tiles: breadth-first for z0..z5, depth-first for z6
	sortTileCoordinates(config.baseZoom, options.threadNum, tileCoordinates);

	std::size_t batchSize = 0;
	for(std::size_t startIndex = 0; startIndex < tileCoordinates.size(); startIndex += batchSize) {
		// Compute how many tiles should be assigned to this batch --
		// higher-zoom tiles are cheaper to compute, lower-zoom tiles more expensive.
		batchSize = 0;
		size_t weight = 0;
		while (weight < 1000 && startIndex + batchSize < tileCoordinates.size()) {
			const auto& zoom = tileCoordinates[startIndex + batchSize].first;
			if (zoom > 12)
				weight++;
			else if (zoom > 11)
				weight += 10;
			else if (zoom > 10)
				weight += 100;
			else
				weight += 1000;

			batchSize++;
		}

		boost::asio::post(pool, [=, &tileCoordinates, &pool, &sharedData, &sources, &attributeStore, &io_mutex, &tilesWritten, &lastTilesWritten]() {
			std::vector<std::string> tileTimings;
			std::size_t endIndex = std::min(tileCoordinates.size(), startIndex + batchSize);
			for(std::size_t i = startIndex; i < endIndex; ++i) {
				unsigned int zoom = tileCoordinates[i].first;
				TileCoordinates coords = tileCoordinates[i].second;

#ifdef CLOCK_MONOTONIC
				timespec start, end;
				if (options.logTileTimings)
					clock_gettime(CLOCK_MONOTONIC, &start);
#endif

				std::vector<std::vector<OutputObjectID>> data;
				for (auto source : sources) {
					data.emplace_back(source->getObjectsForTile(sortOrders, zoom, coords));
				}
				outputProc(sharedData, sources, attributeStore, data, coords, zoom);

#ifdef CLOCK_MONOTONIC
				if (options.logTileTimings) {
					clock_gettime(CLOCK_MONOTONIC, &end);
					uint64_t tileNs = 1e9 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
					std::string output = "z" + std::to_string(zoom) + "/" + std::to_string(coords.x) + "/" + std::to_string(coords.y) + " took " + std::to_string(tileNs/1e6) + " ms";
					tileTimings.push_back(output);
				}
#endif
			}

			if (options.logTileTimings) {
				const std::lock_guard<std::mutex> lock(io_mutex);
				std::cout << std::endl;
				for (const auto& output : tileTimings)
					std::cout << output << std::endl;
			}

			tilesWritten += (endIndex - startIndex); 

			if (io_mutex.try_lock()) {
				uint64_t written = tilesWritten.load();

				if (written >= lastTilesWritten + tileCoordinates.size() / 100 || ISATTY) {
					lastTilesWritten = written;
					// Show progress grouped by z6 (or lower)
					size_t z = tileCoordinates[startIndex].first;
					size_t x = tileCoordinates[startIndex].second.x;
					size_t y = tileCoordinates[startIndex].second.y;
					if (z > CLUSTER_ZOOM) {
						x = x / (1 << (z - CLUSTER_ZOOM));
						y = y / (1 << (z - CLUSTER_ZOOM));
						z = CLUSTER_ZOOM;
					}
					cout << "z" << z << "/" << x << "/" << y << ", writing tile " << written << " of " << tileCoordinates.size() << "               \r" << std::flush;
				}
				io_mutex.unlock();
			}
		});
	}
	// Wait for all tasks in the pool to complete.
	pool.join();

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
