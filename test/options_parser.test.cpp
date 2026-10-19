#include <iostream>
#include "external/minunit.h"
#include "options_parser.h"

const char* PROGRAM_NAME = "./tilemaker";
using namespace OptionsParser;

Options parse(std::vector<std::string>& args) {
	const char* argv[100];

	argv[0] = PROGRAM_NAME;
	for(int i = 0; i < args.size(); i++)
		argv[1 + i] = args[i].data();

	return parse(1 + args.size(), argv);
}

#define ASSERT_THROWS(MESSAGE, ...) \
{ \
	std::vector<std::string> args = { __VA_ARGS__ }; \
	bool threw = false; \
	try { \
		auto opts = parse(args); \
	} catch(OptionsParser::OptionException& e) { \
		threw = std::string(e.what()).find(MESSAGE) != std::string::npos; \
	} \
	if (!threw) mu_check((std::string("expected exception with ") + MESSAGE).empty()); \
}

MU_TEST(test_options_parser) {
	// No args is invalid.
	ASSERT_THROWS("You must specify an output file");

	// Output without input is valid
	{
		std::vector<std::string> args = {"--output", "foo.mbtiles"};
		auto opts = parse(args);
		mu_check(opts.inputFiles.size() == 0);
	}

	// You can ask for --help.
	{
		std::vector<std::string> args = {"--help"};
		auto opts = parse(args);
		mu_check(opts.showHelp);
	}

	// Common happy path is output and input
	{
		std::vector<std::string> args = {"--output", "foo.mbtiles", "--input", "area.o5m"};
		auto opts = parse(args);
		mu_check(opts.inputFiles.size() == 1);
		mu_check(opts.inputFiles[0] == "area.o5m");
		mu_check(opts.outputFile == "foo.mbtiles");
		mu_check(opts.outputMode == OutputMode::MBTiles);
		mu_check(!opts.useDatabase);
	}

	// Several input files
	{
		std::vector<std::string> args = {"--output", "foo.pmtiles", "--input", "a.osm", "--input", "b.pbf"};
		auto opts = parse(args);
		mu_check(opts.inputFiles.size() == 2);
		mu_check(opts.inputFiles[1] == "b.pbf");
		mu_check(opts.outputMode == OutputMode::PMTiles);
	}

	// The map database, with the PG* environment variables
	{
		std::vector<std::string> args = {"--output", "foo.mbtiles", "--db", "--bbox", "-1.1,50.79,-1.08,50.8"};
		auto opts = parse(args);
		mu_check(opts.useDatabase);
		mu_check(opts.dbConnection.empty());
		mu_check(opts.tablePrefix == "pycrocosm_mod_");
		mu_check(opts.inputFiles.empty());
	}

	// The map database, with a connection string and table set
	{
		std::vector<std::string> args = {"--output", "foo.mbtiles", "--db=dbname=map", "--table-prefix", "other_", "--bbox", "-1.1,50.79,-1.08,50.8"};
		auto opts = parse(args);
		mu_check(opts.useDatabase);
		mu_check(opts.dbConnection == "dbname=map");
		mu_check(opts.tablePrefix == "other_");
	}

	ASSERT_THROWS("--db needs a --bbox", "--output", "foo.mbtiles", "--db");

	ASSERT_THROWS("Couldn't open .json config", "--input", "foo", "--output", "bar", "--config", "nonexistent-config.json");
	ASSERT_THROWS("Couldn't open .lua script", "--input", "foo", "--output", "bar", "--process", "nonexistent-script.lua");
}

MU_TEST_SUITE(test_suite_options_parser) {
	MU_RUN_TEST(test_options_parser);
}

int main() {
	MU_RUN_SUITE(test_suite_options_parser);
	MU_REPORT();
	return MU_EXIT_CODE;
}
