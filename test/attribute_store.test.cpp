#include <iostream>
#include <algorithm>
#include <vector>
#include "external/minunit.h"
#include "attribute_store.h"

MU_TEST(test_attribute_store) {
	AttributeStore store;

	mu_check(store.size() == 0);

	AttributeSet s1;
	store.addAttribute(s1, "str1", std::string("someval"), 0);
	store.addAttribute(s1, "str2", std::string("a very long string"), 14);
	store.addAttribute(s1, "bool1", false, 0);
	store.addAttribute(s1, "bool2", true, 0);
	store.addAttribute(s1, "double1", (double)42.0, 4);
	store.addAttribute(s1, "int1", 43, 8);

	const auto s1Index = store.add(s1);

	mu_check(store.size() == 1);

	const auto s1Pairs = store.getUnsafe(s1Index);
	mu_check(s1Pairs.size() == 6);
	const auto str1 = std::find_if(s1Pairs.begin(), s1Pairs.end(), [&store](auto ap) {
			return ap->keyIndex == store.keyStore.key2index("str1");
	});
	mu_check(str1 != s1Pairs.end());
	mu_check((*str1)->hasStringValue());
	mu_check((*str1)->stringValue() == "someval");

	const auto str2 = std::find_if(s1Pairs.begin(), s1Pairs.end(), [&store](auto ap) {
			return ap->keyIndex == store.keyStore.key2index("str2");
	});
	mu_check(str2 != s1Pairs.end());
	mu_check((*str2)->hasStringValue());
	mu_check((*str2)->stringValue() == "a very long string");
	mu_check((*str2)->minzoom == 14);

	const auto bool1 = std::find_if(s1Pairs.begin(), s1Pairs.end(), [&store](auto ap) {
			return ap->keyIndex == store.keyStore.key2index("bool1");
	});
	mu_check(bool1 != s1Pairs.end());
	mu_check((*bool1)->hasBoolValue());
	mu_check((*bool1)->boolValue() == false);

	const auto bool2 = std::find_if(s1Pairs.begin(), s1Pairs.end(), [&store](auto ap) {
			return ap->keyIndex == store.keyStore.key2index("bool2");
	});
	mu_check(bool2 != s1Pairs.end());
	mu_check((*bool2)->hasBoolValue());
	mu_check((*bool2)->boolValue() == true);

	const auto double1 = std::find_if(s1Pairs.begin(), s1Pairs.end(), [&store](auto ap) {
			return ap->keyIndex == store.keyStore.key2index("double1");
	});
	mu_check(double1 != s1Pairs.end());
	mu_check((*double1)->hasFloatValue());
	mu_check((*double1)->floatValue() == 42);
	mu_check((*double1)->minzoom == 4);

	const auto int1 = std::find_if(s1Pairs.begin(), s1Pairs.end(), [&store](auto ap) {
			return ap->keyIndex == store.keyStore.key2index("int1");
	});
	mu_check(int1 != s1Pairs.end());
	mu_check((*int1)->hasIntValue());
	mu_check((*int1)->intValue() == 43);
	mu_check((*int1)->minzoom == 8);
}

MU_TEST(test_attribute_store_reuses) {
	AttributeStore store;

	mu_check(store.size() == 0);

	{
		AttributeSet s1a;
		store.addAttribute(s1a, "str1", std::string("someval"), 0);
		const auto s1aIndex = store.add(s1a);

		AttributeSet s1b;
		store.addAttribute(s1b, "str1", std::string("someval"), 0);
		const auto s1bIndex = store.add(s1b);

		mu_check(s1aIndex == s1bIndex);
	}

	{
		AttributeSet s1a;
		store.addAttribute(s1a, "str1", std::string("this is a very long string"), 0);
		const auto s1aIndex = store.add(s1a);

		AttributeSet s1b;
		store.addAttribute(s1b, "str1", std::string("this is a very long string"), 0);
		const auto s1bIndex = store.add(s1b);

		mu_check(s1aIndex == s1bIndex);
	}
}

MU_TEST(test_attribute_key_store) {
	AttributeKeyStore keys;

	// Keys are numbered from 1, and keep their numbers
	for (int i = 1; i <= 1000; i++)
		mu_check(keys.key2index("key" + std::to_string(i)) == i);
	mu_check(keys.key2index("key512") == 512);
	mu_check(keys.getKey(512) == "key512");
}

MU_TEST(test_attribute_set_replaces_key) {
	AttributeStore store;
	AttributeSet s;
	store.addAttribute(s, "class", std::string("road"), 0);
	store.addAttribute(s, "rank", 3, 0);
	s.removePairWithKey(store.pairStore, store.keyStore.key2index("class"));
	store.addAttribute(s, "class", std::string("path"), 0);

	const auto pairs = store.getUnsafe(store.add(s));
	mu_check(pairs.size() == 2);
	int classes = 0;
	for (const auto pair : pairs)
		if (pair->keyIndex == store.keyStore.key2index("class")) {
			classes++;
			mu_check(pair->stringValue() == "path");
		}
	mu_check(classes == 1);
}

MU_TEST_SUITE(test_suite_attribute_store) {
	MU_RUN_TEST(test_attribute_store);
	MU_RUN_TEST(test_attribute_store_reuses);
	MU_RUN_TEST(test_attribute_key_store);
	MU_RUN_TEST(test_attribute_set_replaces_key);
}

int main() {
	MU_RUN_SUITE(test_suite_attribute_store);
	MU_REPORT();
	return MU_EXIT_CODE;
}
