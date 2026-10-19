#include <iostream>
#include <stdexcept>
#include "external/minunit.h"
#include "helpers.h"

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
		std::string unzipped;
		decompress_string(unzipped, gzipped.data(), gzipped.size(), true);
		mu_check(unzipped == input);
	}

	std::string gzipped = compress_string(input, -1, true);
	std::string unzipped;
	decompress_string(unzipped, gzipped.data(), gzipped.size(), true);
	mu_check(unzipped == input);

}

MU_TEST(test_compression_zlib) {
	std::string input = "a random string to be compressed";

	for (int level = 1; level < 9; level++) {
		std::string gzipped = compress_string(input, level, false);
		std::string unzipped;
		decompress_string(unzipped, gzipped.data(), gzipped.size(), false);
		mu_check(unzipped == input);
	}

	std::string gzipped = compress_string(input, -1, false);
	std::string unzipped;
	decompress_string(unzipped, gzipped.data(), gzipped.size(), false);
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
