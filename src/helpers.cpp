#include <string>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <zlib.h>
#include "helpers.h"

using namespace std;

// Bounding box string parsing

double bboxElementFromStr(const std::string& number) {
	try {
		return boost::lexical_cast<double>(number);
	} catch (boost::bad_lexical_cast&) {
		std::cerr << "Failed to parse coordinate " << number << std::endl;
		exit(1);
	}
}

// Split bounding box provided as a comma-separated list of coordinates.
std::vector<std::string> parseBox(const std::string& bbox) {
	std::vector<std::string> bboxParts;
	if (!bbox.empty()) {
		boost::split(bboxParts, bbox, boost::is_any_of(","));
		if (bboxParts.size() != 4) {
			std::cerr << "Bounding box must contain 4 elements: minlon,minlat,maxlon,maxlat" << std::endl;
			exit(1);
		}
	}
	return bboxParts;
}

// Compress a string with zlib at the given level, as gzip or zlib data
std::string compress_string(const std::string& str,
                            int compressionlevel,
                            bool asGzip) {
	z_stream zs = {};
	// windowBits 15, plus 16 for a gzip header and trailer
	if (deflateInit2(&zs, compressionlevel, Z_DEFLATED, asGzip ? 15 + 16 : 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		throw std::runtime_error("deflateInit2 failed");

	std::string rv(deflateBound(&zs, str.size()), '\0');
	zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(str.data()));
	zs.avail_in = str.size();
	zs.next_out = reinterpret_cast<Bytef*>(&rv[0]);
	zs.avail_out = rv.size();
	int result = deflate(&zs, Z_FINISH);
	rv.resize(zs.total_out);
	deflateEnd(&zs);
	if (result != Z_STREAM_END)
		throw std::runtime_error("deflate failed");
	return rv;
}

// Parse a Boost error
std::string boost_validity_error(unsigned failure) {
	switch (failure) {
		case 10: return "too few points";
		case 11: return "wrong topological dimension";
		case 12: return "spikes (nodes go back on themselves)";
		case 13: return "consecutive duplicate points";
		case 20: return "not been closed";
		case 21: return "self-intersections";
		case 22: return "the wrong orientation";
		case 30: return "interior rings outside";
		case 31: return "nested interior rings";
		case 32: return "disconnected interior (contains polygons whose interiors are not disjoint)";
		case 40: return "intersecting interiors";
		default: return "something mysterious wrong with it, Boost validity_failure_type " + to_string(failure);
	}
}

FilePtr openFile(const std::string &filename, const char *mode) {
	FilePtr fp(fopen(filename.c_str(), mode));
	if (fp == nullptr)
		throw std::runtime_error("unable to open " + filename);
	return fp;
}

// Given a file, attempt to divide it into N chunks, with each chunk separated
// by a newline.
//
// Useful for dividing a JSON lines file into blocks suitable for parallel processing.
