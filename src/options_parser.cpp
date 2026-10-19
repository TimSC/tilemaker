#include "options_parser.h"

#include <thread>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include "helpers.h"

#ifndef TM_VERSION
#define TM_VERSION (version not set)
#endif
#define STR1(x)  #x
#define STR(x)  STR1(x)

using namespace std;
namespace po = boost::program_options;

po::options_description getParser(OptionsParser::Options& options) {
	po::options_description desc("tilemaker " STR(TM_VERSION) "\nRender the OpenStreetMap data of an area into vector tiles\n\nAvailable options");
	desc.add_options()
		("help",                                                                 "show help message")
		("input",  po::value< vector<string> >(&options.inputFiles),                     "source .osm, .o5m or .pbf file covering the area")
		("output", po::value< string >(&options.outputFile),                             "target directory or .mbtiles/.pmtiles file")
		("db",     po::value< string >(&options.dbConnection)->implicit_value(""),       "read from the pycrocosm map database, as a libpq connection string; alone, the PG* environment variables are used (keep the password in PGPASSWORD or ~/.pgpass)")
		("table-prefix", po::value< string >(&options.tablePrefix)->default_value("pycrocosm_mod_"), "map database table set to read (pgmap's dbtablemodifyprefix)")
		("tile",   po::value< string >(),                                                 "render just this tile, as z/x/y (the area read is the tile and a margin around it)")
		("bbox",   po::value< string >(&options.bbox),                                   "area to render (needed with --db, or if the input does not give its bounds), example: minlon,minlat,maxlon,maxlat")
		("merge"  ,po::bool_switch(&options.mergeSqlite),                                "merge with existing .mbtiles (overwrites otherwise)")
		("config", po::value< string >(&options.jsonFile)->default_value("config.json"), "config JSON file")
		("process",po::value< string >(&options.luaFile)->default_value("process.lua"),  "tag-processing Lua file")
		("quiet",  po::bool_switch(&options.quiet),                                      "quiet, suppress standard output")
		("verbose",po::bool_switch(&options.verbose),                                   "verbose error output")
		("skip-integrity",po::bool_switch(&options.osm.skipIntegrity),                       "don't enforce way/node integrity")
		("log-tile-timings", po::bool_switch(&options.logTileTimings), "log how long each tile takes");
	po::options_description performance("Performance options");
	performance.add_options()
		("threads",po::value<uint32_t>(&options.threadNum)->default_value(0),              "number of threads (automatically detected if 0)")
			;

	desc.add(performance);
	return desc;
}

void OptionsParser::showHelp() {
	Options options;
	auto parser = getParser(options);
	std::cout << parser << std::endl;
}

OptionsParser::Options OptionsParser::parse(const int argc, const char* argv[]) {
	Options options;

	po::options_description desc = getParser(options);
	po::positional_options_description p;
	p.add("input", 1).add("output", 1);

	po::variables_map vm;
	try {
		po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
	} catch (const po::unknown_option& ex) {
		throw OptionException{"Unknown option: " + ex.get_option_name()};
	} catch (const po::error& ex) {
		throw OptionException{ex.what()};
	}
	po::notify(vm);

	if (vm.count("help")) {
		options.showHelp = true;
		return options;
	}
	if (vm.count("tile")) {
		const std::string tile = vm["tile"].as<std::string>();
		char end;
		if (sscanf(tile.c_str(), "%u/%u/%u%c", &options.tileZoom, &options.tileX, &options.tileY, &end) != 3 ||
		    options.tileZoom > 22 || options.tileX >= (1u << options.tileZoom) || options.tileY >= (1u << options.tileZoom)) {
			throw OptionException{ "--tile should be z/x/y, such as 14/8142/5499" };
		}
		options.hasTile = true;
	}

	options.useDatabase = vm.count("db") > 0;
	if (options.useDatabase && options.bbox.empty() && !options.hasTile) {
		throw OptionException{ "--db needs a --bbox or a --tile." };
	}
	if (vm.count("output") == 0) {
		throw OptionException{ "You must specify an output file or directory. Run with --help to find out more." };
	}

	if (ends_with(options.outputFile, ".mbtiles") || ends_with(options.outputFile, ".sqlite")) {
		options.outputMode = OutputMode::MBTiles;
	} else if (ends_with(options.outputFile, ".pmtiles")) {
		options.outputMode = OutputMode::PMTiles;
	}

	if (options.threadNum == 0) {
		options.threadNum = max(thread::hardware_concurrency(), 1u);
	}

	// ---- Check config
	if (!boost::filesystem::exists(options.jsonFile)) {
		throw OptionException{ "Couldn't open .json config: " + options.jsonFile };
	}
	if (!boost::filesystem::exists(options.luaFile)) {
		throw OptionException{"Couldn't open .lua script: " + options.luaFile };
	}

	return options;
}
