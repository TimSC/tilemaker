#include <string>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include "external/libdeflate/libdeflate.h"
#include "helpers.h"

#define MOD_GZIP_ZLIB_WINDOWSIZE 15
#define MOD_GZIP_ZLIB_CFACTOR 9
#define MOD_GZIP_ZLIB_BSIZE 8096

using namespace std;

class Compressor {
public:
	int level;
	libdeflate_compressor* compressor;

	Compressor(int level): level(level), compressor(NULL) {
		setLevel(level);
	}

	void setLevel(int level) {
		libdeflate_free_compressor(compressor);
		this->level = level;
		compressor = libdeflate_alloc_compressor(level);

		if (!compressor)
			throw std::runtime_error("libdeflate_alloc_compressor failed (level=" + std::to_string(level) + ")");
	}

	Compressor & operator=(const Compressor&) = delete;
	Compressor(const Compressor&) = delete;

	~Compressor() {
		libdeflate_free_compressor(compressor);
	}
};

class Decompressor {
public:
	libdeflate_decompressor* decompressor;

	Decompressor(): decompressor(NULL) {
		decompressor = libdeflate_alloc_decompressor();

		if (!decompressor)
			throw std::runtime_error("libdeflate_alloc_decompressor failed");
	}

	Decompressor & operator=(const Decompressor&) = delete;
	Decompressor(const Decompressor&) = delete;

	~Decompressor() {
		libdeflate_free_decompressor(decompressor);
	}
};


static Compressor compressor(6);
static Decompressor decompressor;

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

// Compress a STL string using zlib with given compression level, and return the binary data
std::string compress_string(const std::string& str,
                            int compressionlevel,
                            bool asGzip) {
	if (compressionlevel == Z_DEFAULT_COMPRESSION)
		compressionlevel = 6;

	if (compressionlevel != compressor.level)
		compressor.setLevel(compressionlevel);

	std::string rv;
	if (asGzip) {
		size_t maxSize = libdeflate_gzip_compress_bound(compressor.compressor, str.size());
		rv.resize(maxSize);

		size_t compressedSize = libdeflate_gzip_compress(compressor.compressor, str.data(), str.size(), &rv[0], maxSize);
		if (compressedSize == 0)
			throw std::runtime_error("libdeflate_gzip_compress failed");
		rv.resize(compressedSize);
	} else {
		size_t maxSize = libdeflate_zlib_compress_bound(compressor.compressor, str.size());
		rv.resize(maxSize);

		size_t compressedSize = libdeflate_zlib_compress(compressor.compressor, str.data(), str.size(), &rv[0], maxSize);
		if (compressedSize == 0)
			throw std::runtime_error("libdeflate_zlib_compress failed");
		rv.resize(compressedSize);
	}

	return rv;
}

// Decompress an STL string using zlib and return the original data.
// The output buffer is passed in; callers are meant to re-use the buffer such
// that eventually no allocations are needed when decompressing.
void decompress_string(std::string& output, const char* input, uint32_t inputSize, bool asGzip) {
	size_t uncompressedSize;

	if (output.size() < inputSize)
		output.resize(inputSize);

	while (true) {
		libdeflate_result rv = LIBDEFLATE_BAD_DATA;

		if (asGzip) {
			rv = libdeflate_gzip_decompress(
				decompressor.decompressor,
				input,
				inputSize,
				&output[0],
				output.size(),
				&uncompressedSize
			);
		} else {
			rv = libdeflate_zlib_decompress(
				decompressor.decompressor,
				input,
				inputSize,
				&output[0],
				output.size(),
				&uncompressedSize
			);
		}

		if (rv == LIBDEFLATE_SUCCESS) {
			output.resize(uncompressedSize);
			return;
		}

		if (rv == LIBDEFLATE_INSUFFICIENT_SPACE) {
			output.resize((output.size() + 128) * 2);
		} else
			throw std::runtime_error("libdeflate_gzip_decompress failed");
	}
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
