#include "catch2.hpp"

#include "../src/ofxGgmlBasic.h"
#include "../src/ofxGgml.h"
#include "../src/ofxGgmlModalities.h"
#include "../src/ofxGgmlWorkflows.h"

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
#include "../src/ofxGgmlCompanionWorkflows.h"
#endif

TEST_CASE("Public layered headers compile in the configured feature tier", "[headers]") {
	SUCCEED("Public layered headers compiled successfully.");
}
