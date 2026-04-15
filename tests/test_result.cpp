#include "catch2.hpp"
#include "../src/core/ofxGgmlResult.h"

#include <cstring>

TEST_CASE("Result with success value", "[result]") {
	SECTION("Integer result") {
		Result<int> r(42);
		REQUIRE(r.isOk());
		REQUIRE_FALSE(r.isError());
		REQUIRE(r.value() == 42);
	}

	SECTION("String result") {
		Result<std::string> r(std::string("success"));
		REQUIRE(r.isOk());
		REQUIRE(r.value() == "success");
	}

	SECTION("Bool conversion") {
		Result<int> r(10);
		REQUIRE(bool(r) == true);
	}
}

TEST_CASE("Result with error", "[result]") {
	SECTION("Error code only") {
		Result<int> r(ofxGgmlErrorCode::InvalidArgument);
		REQUIRE_FALSE(r.isOk());
		REQUIRE(r.isError());
		REQUIRE(r.error().code == ofxGgmlErrorCode::InvalidArgument);
	}

	SECTION("Error with message") {
		Result<int> r(ofxGgmlErrorCode::OutOfMemory, "Not enough memory");
		REQUIRE(r.isError());
		REQUIRE(r.error().message == "Not enough memory");
		REQUIRE(r.error().toString().find("OutOfMemory") != std::string::npos);
	}

	SECTION("Bool conversion for error") {
		Result<int> r(ofxGgmlErrorCode::UnknownError);
		REQUIRE(bool(r) == false);
	}
}

TEST_CASE("Result valueOr", "[result]") {
	SECTION("Success returns value") {
		Result<int> r(100);
		REQUIRE(r.valueOr(0) == 100);
	}

	SECTION("Error returns default") {
		Result<int> r(ofxGgmlErrorCode::InvalidArgument);
		REQUIRE(r.valueOr(999) == 999);
	}
}

TEST_CASE("Result void specialization", "[result]") {
	SECTION("Success") {
		Result<void> r;
		REQUIRE(r.isOk());
		REQUIRE_FALSE(r.isError());
	}

	SECTION("Error") {
		Result<void> r(ofxGgmlErrorCode::ComputeFailed, "Computation failed");
		REQUIRE(r.isError());
		REQUIRE(r.error().code == ofxGgmlErrorCode::ComputeFailed);
		REQUIRE(r.error().message == "Computation failed");
	}

	SECTION("Bool conversion") {
		Result<void> success;
		Result<void> failure(ofxGgmlErrorCode::UnknownError);

		REQUIRE(bool(success) == true);
		REQUIRE(bool(failure) == false);
	}
}

TEST_CASE("ofxGgmlError", "[result]") {
	SECTION("Default constructor") {
		ofxGgmlError err;
		REQUIRE_FALSE(err.hasError());
		REQUIRE(err.code == ofxGgmlErrorCode::None);
	}

	SECTION("Error with code") {
		ofxGgmlError err(ofxGgmlErrorCode::BackendInitFailed);
		REQUIRE(err.hasError());
		REQUIRE(err.codeString() == "BackendInitFailed");
	}

	SECTION("Error toString") {
		ofxGgmlError err(ofxGgmlErrorCode::ModelLoadFailed, "File not found");
		std::string str = err.toString();
		REQUIRE(str.find("ModelLoadFailed") != std::string::npos);
		REQUIRE(str.find("File not found") != std::string::npos);
	}
}

TEST_CASE("Result copy and move", "[result]") {
	SECTION("Copy constructor") {
		Result<int> r1(42);
		Result<int> r2(r1);
		REQUIRE(r2.isOk());
		REQUIRE(r2.value() == 42);
	}

	SECTION("Move constructor") {
		Result<std::string> r1(std::string("test"));
		Result<std::string> r2(std::move(r1));
		REQUIRE(r2.isOk());
		REQUIRE(r2.value() == "test");
	}

	SECTION("Copy assignment") {
		Result<int> r1(100);
		Result<int> r2(ofxGgmlErrorCode::UnknownError);
		r2 = r1;
		REQUIRE(r2.isOk());
		REQUIRE(r2.value() == 100);
	}

	SECTION("Move assignment") {
		Result<int> r1(200);
		Result<int> r2(ofxGgmlErrorCode::InvalidArgument);
		r2 = std::move(r1);
		REQUIRE(r2.isOk());
		REQUIRE(r2.value() == 200);
	}
}

TEST_CASE("Result exception safety", "[result]") {
	SECTION("value() on error throws") {
		Result<int> r(ofxGgmlErrorCode::InvalidArgument, "Invalid");
		REQUIRE_THROWS(r.value());
	}

	SECTION("error() on success throws") {
		Result<int> r(42);
		REQUIRE_THROWS(r.error());
	}
}

TEST_CASE("Error code coverage", "[result]") {
	// Test all error codes have string representations
	std::vector<ofxGgmlErrorCode> codes = {
		ofxGgmlErrorCode::None,
		ofxGgmlErrorCode::BackendInitFailed,
		ofxGgmlErrorCode::DeviceNotFound,
		ofxGgmlErrorCode::OutOfMemory,
		ofxGgmlErrorCode::GraphNotBuilt,
		ofxGgmlErrorCode::GraphAllocFailed,
		ofxGgmlErrorCode::InvalidTensor,
		ofxGgmlErrorCode::TensorShapeMismatch,
		ofxGgmlErrorCode::ComputeFailed,
		ofxGgmlErrorCode::SynchronizationFailed,
		ofxGgmlErrorCode::AsyncOperationPending,
		ofxGgmlErrorCode::ModelLoadFailed,
		ofxGgmlErrorCode::ModelFormatInvalid,
		ofxGgmlErrorCode::ModelWeightUploadFailed,
		ofxGgmlErrorCode::InferenceExecutableMissing,
		ofxGgmlErrorCode::InferenceProcessFailed,
		ofxGgmlErrorCode::InferenceOutputInvalid,
		ofxGgmlErrorCode::InvalidArgument,
		ofxGgmlErrorCode::NotImplemented,
		ofxGgmlErrorCode::UnknownError
	};

	for (auto code : codes) {
		ofxGgmlError err(code);
		REQUIRE(std::strlen(err.codeString()) > 0);
	}
}
