#ifndef OPTIONS_PARSER_H
#define OPTIONS_PARSER_H

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace OptionsParser {
	struct OptionException : std::exception {
		OptionException(std::string message): message(message) {}

		/// Returns the explanatory string.
		const char* what() const noexcept override {
				return message.data();
		}

		private:
			std::string message;
	};

	enum class OutputMode: char { File = 0, MBTiles = 1, PMTiles = 2 };

	struct OsmOptions {
		bool skipIntegrity = false;
	};

	struct Options {
		std::vector<std::string> inputFiles;
		bool useDatabase = false;
		std::string dbConnection;
		std::string tablePrefix;
		std::string luaFile;
		std::string jsonFile;
		uint32_t threadNum = 0;
		std::string outputFile;
		std::string bbox;

		// --tile: render just this tile
		bool hasTile = false;
		unsigned tileZoom = 0, tileX = 0, tileY = 0;

		OsmOptions osm;
		bool showHelp = false;
		bool quiet = false;
		bool verbose = false;
		bool mergeSqlite = false;
		OutputMode outputMode = OutputMode::File;
		bool logTileTimings = false;
	};

	Options parse(const int argc, const char* argv[]);
	void showHelp();
};

#endif
