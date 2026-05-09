#include "test_harness.h"
#include "../src/ofxGgml.h"

OFXGGML_TEST(public_core_header_compiles) {
	OFXGGML_REQUIRE(OFXGGML_VERSION_MAJOR == 2);
	OFXGGML_REQUIRE(OFXGGML_HAS_SAM3 == 0);
}
