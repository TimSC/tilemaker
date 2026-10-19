#include <iostream>
#include <stdexcept>
#include "external/minunit.h"
#include "helpers.h"

// Decompresses gzip (asGzip) or zlib data, to check compress_string
static std::string decompress(const std::string& input, bool asGzip) {
	z_stream zs = {};
	inflateInit2(&zs, asGzip ? 15 + 16 : 15);
	zs.next_in = (Bytef*)input.data();
	zs.avail_in = input.size();
	char buffer[4096];
	zs.next_out = (Bytef*)buffer;
	zs.avail_out = sizeof(buffer);
	int result = inflate(&zs, Z_FINISH);
	std::string output(buffer, sizeof(buffer) - zs.avail_out);
	inflateEnd(&zs);
	if (result != Z_STREAM_END) throw std::runtime_error("decompression failed");
	return output;
}

MU_TEST(test_open_file) {
	FilePtr fp = openFile("test/test.jsonl", "r");
	mu_check(fp != nullptr);
	mu_check(fgetc(fp.get()) == '{');

	bool caughtException = false;
	try {
		openFile("test/this-file-does-not-exist", "r");
	} catch (const std::runtime_error&) {
		caughtException = true;
	}
	mu_check(caughtException);
}

MU_TEST(test_compression_gzip) {
	std::string input = "a random string to be compressed";

	for (int level = 1; level < 9; level++) {
		std::string gzipped = compress_string(input, level, true);
		std::string unzipped = decompress(gzipped, true);
		mu_check(unzipped == input);
	}

	std::string gzipped = compress_string(input, -1, true);
	std::string unzipped = decompress(gzipped, true);
	mu_check(unzipped == input);

}

MU_TEST(test_compression_zlib) {
	std::string input = "a random string to be compressed";

	for (int level = 1; level < 9; level++) {
		std::string gzipped = compress_string(input, level, false);
		std::string unzipped = decompress(gzipped, false);
		mu_check(unzipped == input);
	}

	std::string gzipped = compress_string(input, -1, false);
	std::string unzipped = decompress(gzipped, false);
	mu_check(unzipped == input);
}


MU_TEST_SUITE(test_suite_helpers) {
	MU_RUN_TEST(test_open_file);
	MU_RUN_TEST(test_compression_gzip);
	MU_RUN_TEST(test_compression_zlib);
}

int main() {
	MU_RUN_SUITE(test_suite_helpers);
	MU_REPORT();
	return MU_EXIT_CODE;
}
