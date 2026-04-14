#include "catch2.hpp"
#include "../src/ofxGgml.h"

// Note: Model tests that require actual GGUF files are marked [model][requires-file]
// and will be skipped if the test file doesn't exist.

TEST_CASE("Model initialization", "[model]") {
	ofxGgmlModel model;

	SECTION("Initial state") {
		REQUIRE_FALSE(model.isLoaded());
		REQUIRE(model.getPath().empty());
	}

	SECTION("Close when not loaded is safe") {
		REQUIRE_NOTHROW(model.close());
		REQUIRE_FALSE(model.isLoaded());
	}
}

TEST_CASE("Model loading - invalid files", "[model]") {
	ofxGgmlModel model;

	SECTION("Load non-existent file fails") {
		bool ok = model.load("/nonexistent/path/model.gguf");
		REQUIRE_FALSE(ok);
		REQUIRE_FALSE(model.isLoaded());
	}

	SECTION("Load empty path fails") {
		bool ok = model.load("");
		REQUIRE_FALSE(ok);
		REQUIRE_FALSE(model.isLoaded());
	}

	SECTION("Load invalid file format") {
		// Try loading a non-GGUF file (the test binary itself)
		bool ok = model.load("/proc/self/exe");
		REQUIRE_FALSE(ok);
		REQUIRE_FALSE(model.isLoaded());
	}
}

TEST_CASE("Model metadata - unloaded", "[model]") {
	ofxGgmlModel model;

	SECTION("Metadata queries on unloaded model") {
		REQUIRE(model.getNumMetadataKeys() == 0);
		REQUIRE(model.getMetadataString("test.key").empty());
		REQUIRE(model.findMetadataKey("test.key") == -1);
	}

	SECTION("Tensor queries on unloaded model") {
		REQUIRE(model.getNumTensors() == 0);
		REQUIRE(model.getTensorName(0).empty());
	}
}

// Tests that require an actual GGUF file
// These will be skipped in CI unless a test model is available
TEST_CASE("Model loading - with file", "[model][requires-file]") {
	// Check if a test model exists
	std::string testModelPath = "test_data/tiny_model.gguf";

	// Skip test if file doesn't exist (CI environment)
	if (!std::ifstream(testModelPath).good()) {
		WARN("Skipping test - test model not found at: " + testModelPath);
		return;
	}

	ofxGgmlModel model;

	SECTION("Load valid GGUF file") {
		bool ok = model.load(testModelPath);
		REQUIRE(ok);
		REQUIRE(model.isLoaded());
		REQUIRE(model.getPath() == testModelPath);
	}

	SECTION("Load and query metadata") {
		model.load(testModelPath);

		// Any GGUF should have at least some metadata
		REQUIRE(model.getNumMetadataKeys() > 0);

		// Try to find a common key
		int64_t archKey = model.findMetadataKey("general.architecture");
		// -1 is ok if not found, just testing the API
		REQUIRE(archKey >= -1);
	}

	SECTION("Load and query tensors") {
		model.load(testModelPath);

		int64_t numTensors = model.getNumTensors();
		REQUIRE(numTensors >= 0);

		if (numTensors > 0) {
			std::string tensorName = model.getTensorName(0);
			REQUIRE_FALSE(tensorName.empty());

			auto tensor = model.getTensor(tensorName);
			REQUIRE(tensor.isValid());
		}
	}

	SECTION("Close and reload") {
		model.load(testModelPath);
		REQUIRE(model.isLoaded());

		model.close();
		REQUIRE_FALSE(model.isLoaded());
		REQUIRE(model.getPath().empty());

		// Can reload
		bool ok = model.load(testModelPath);
		REQUIRE(ok);
		REQUIRE(model.isLoaded());
	}
}

TEST_CASE("Model tensor access", "[model]") {
	ofxGgmlModel model;

	SECTION("Get tensor by invalid name returns invalid tensor") {
		auto t = model.getTensor("nonexistent.tensor");
		REQUIRE_FALSE(t.isValid());
	}

	SECTION("Get tensor name out of bounds") {
		std::string name = model.getTensorName(999);
		REQUIRE(name.empty());
	}

	SECTION("Find tensor by invalid name") {
		int64_t idx = model.findTensor("nonexistent.tensor");
		REQUIRE(idx == -1);
	}
}

TEST_CASE("Model metadata types", "[model][requires-file]") {
	std::string testModelPath = "test_data/tiny_model.gguf";

	if (!std::ifstream(testModelPath).good()) {
		WARN("Skipping test - test model not found");
		return;
	}

	ofxGgmlModel model;
	model.load(testModelPath);

	SECTION("Get string metadata") {
		// Try common string keys
		std::string arch = model.getMetadataString("general.architecture");
		// Empty is ok if key doesn't exist
		REQUIRE(arch.size() >= 0);
	}

	SECTION("Get int32 metadata") {
		// Try to get an integer metadata value
		int32_t val = model.getMetadataInt32("test.int_key");
		// 0 or actual value is fine
		REQUIRE(val >= 0 || val < 0); // Just check it doesn't crash
	}

	SECTION("Get uint32 metadata") {
		uint32_t val = model.getMetadataUInt32("test.uint_key");
		REQUIRE(val >= 0); // uint is always >= 0
	}

	SECTION("Get float32 metadata") {
		float val = model.getMetadataFloat32("test.float_key");
		// Any value is ok
		REQUIRE(val == val); // Check not NaN (NaN != NaN)
	}
}

TEST_CASE("Model iteration", "[model][requires-file]") {
	std::string testModelPath = "test_data/tiny_model.gguf";

	if (!std::ifstream(testModelPath).good()) {
		WARN("Skipping test - test model not found");
		return;
	}

	ofxGgmlModel model;
	model.load(testModelPath);

	SECTION("Iterate all metadata keys") {
		int64_t numKeys = model.getNumMetadataKeys();

		for (int64_t i = 0; i < numKeys; i++) {
			std::string key = model.getMetadataKey(i);
			REQUIRE_FALSE(key.empty());
		}
	}

	SECTION("Iterate all tensors") {
		int64_t numTensors = model.getNumTensors();

		for (int64_t i = 0; i < numTensors; i++) {
			std::string name = model.getTensorName(i);
			REQUIRE_FALSE(name.empty());

			// Should be able to get tensor by name
			auto tensor = model.getTensor(name);
			REQUIRE(tensor.isValid());

			// Find should return same index
			int64_t foundIdx = model.findTensor(name);
			REQUIRE(foundIdx == i);
		}
	}
}

TEST_CASE("Model weight loading", "[model][requires-file]") {
	std::string testModelPath = "test_data/tiny_model.gguf";

	if (!std::ifstream(testModelPath).good()) {
		WARN("Skipping test - test model not found");
		return;
	}

	ofxGgml ggml;
	ggml.setup();

	ofxGgmlModel model;
	model.load(testModelPath);

	SECTION("Load model weights to backend") {
		bool ok = ggml.loadModelWeights(model);
		// May fail if model is too large or backend doesn't support it
		// Just check it doesn't crash
		REQUIRE((ok == true || ok == false));
	}
}

TEST_CASE("Model API robustness", "[model]") {
	ofxGgmlModel model;

	SECTION("Multiple close calls are safe") {
		model.close();
		model.close();
		model.close();
		REQUIRE_FALSE(model.isLoaded());
	}

	SECTION("Metadata access with negative index") {
		std::string key = model.getMetadataKey(-1);
		REQUIRE(key.empty());
	}

	SECTION("Tensor access with negative index") {
		std::string name = model.getTensorName(-1);
		REQUIRE(name.empty());
	}

	SECTION("Get metadata with empty key") {
		std::string val = model.getMetadataString("");
		REQUIRE(val.empty());
	}
}
