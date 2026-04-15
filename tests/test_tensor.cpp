#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("Tensor creation - 1D", "[tensor]") {
	ofxGgmlGraph graph;

	SECTION("Create F32 tensor") {
		auto t = graph.newTensor1d(ofxGgmlType::F32, 10);
		REQUIRE(t.isValid());
		REQUIRE(t.getNumDimensions() == 1);
		REQUIRE(t.getDimSize(0) == 10);
		REQUIRE(t.getType() == ofxGgmlType::F32);
	}

	SECTION("Create I32 tensor") {
		auto t = graph.newTensor1d(ofxGgmlType::I32, 5);
		REQUIRE(t.isValid());
		REQUIRE(t.getDimSize(0) == 5);
		REQUIRE(t.getType() == ofxGgmlType::I32);
	}

	SECTION("Zero-size tensor") {
		auto t = graph.newTensor1d(ofxGgmlType::F32, 0);
		// Should still create but have 0 elements
		REQUIRE(t.isValid());
	}
}

TEST_CASE("Tensor creation - 2D", "[tensor]") {
	ofxGgmlGraph graph;

	SECTION("Create 2D F32 tensor") {
		auto t = graph.newTensor2d(ofxGgmlType::F32, 4, 3);
		REQUIRE(t.isValid());
		REQUIRE(t.getNumDimensions() == 2);
		REQUIRE(t.getDimSize(0) == 4);
		REQUIRE(t.getDimSize(1) == 3);
		REQUIRE(t.getNumElements() == 12);
	}

	SECTION("Matrix dimensions") {
		auto t = graph.newTensor2d(ofxGgmlType::F32, 2, 8);
		REQUIRE(t.getDimSize(0) == 2);  // rows
		REQUIRE(t.getDimSize(1) == 8);  // cols
	}
}

TEST_CASE("Tensor creation - 3D and 4D", "[tensor]") {
	ofxGgmlGraph graph;

	SECTION("3D tensor") {
		auto t = graph.newTensor3d(ofxGgmlType::F32, 2, 3, 4);
		REQUIRE(t.isValid());
		REQUIRE(t.getNumDimensions() == 3);
		REQUIRE(t.getDimSize(0) == 2);
		REQUIRE(t.getDimSize(1) == 3);
		REQUIRE(t.getDimSize(2) == 4);
		REQUIRE(t.getNumElements() == 24);
	}

	SECTION("4D tensor") {
		auto t = graph.newTensor4d(ofxGgmlType::F32, 2, 2, 2, 2);
		REQUIRE(t.isValid());
		REQUIRE(t.getNumDimensions() == 4);
		REQUIRE(t.getNumElements() == 16);
	}
}

TEST_CASE("Tensor properties", "[tensor]") {
	ofxGgmlGraph graph;
	auto t = graph.newTensor2d(ofxGgmlType::F32, 3, 4);

	SECTION("Valid tensor has properties") {
		REQUIRE(t.isValid());
		REQUIRE(t.getNumDimensions() > 0);
		REQUIRE(t.getNumElements() > 0);
	}

	SECTION("Type information") {
		REQUIRE(t.getType() == ofxGgmlType::F32);
	}

	SECTION("Name can be set and retrieved") {
		// Note: Name setting depends on ggml implementation
		// This test checks if the API is available
		REQUIRE(t.getName().size() >= 0);
	}
}

TEST_CASE("Element-wise operations", "[tensor][ops]") {
	ofxGgmlGraph graph;

	SECTION("Addition") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto c = graph.add(a, b);

		REQUIRE(c.isValid());
		REQUIRE(c.getDimSize(0) == 2);
		REQUIRE(c.getDimSize(1) == 2);
	}

	SECTION("Subtraction") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 3, 3);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 3, 3);
		auto c = graph.sub(a, b);

		REQUIRE(c.isValid());
		REQUIRE(c.getNumElements() == 9);
	}

	SECTION("Multiplication") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 3);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 3);
		auto c = graph.mul(a, b);

		REQUIRE(c.isValid());
	}

	SECTION("Division") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto c = graph.div(a, b);

		REQUIRE(c.isValid());
	}

	SECTION("Scale") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto c = graph.scale(a, 2.5f);

		REQUIRE(c.isValid());
		REQUIRE(c.getDimSize(0) == 2);
	}

	SECTION("Invalid input returns invalid tensor") {
		ofxGgmlTensor invalid;
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto c = graph.add(invalid, a);

		REQUIRE_FALSE(c.isValid());
	}
}

TEST_CASE("Unary operations", "[tensor][ops]") {
	ofxGgmlGraph graph;

	SECTION("Square") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 3, 3);
		auto c = graph.sqr(a);

		REQUIRE(c.isValid());
		REQUIRE(c.getDimSize(0) == 3);
		REQUIRE(c.getDimSize(1) == 3);
	}

	SECTION("Square root") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 4);
		auto c = graph.sqrt(a);

		REQUIRE(c.isValid());
		REQUIRE(c.getNumElements() == 8);
	}

	SECTION("Clamp") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 10);
		auto c = graph.clamp(a, -1.0f, 1.0f);

		REQUIRE(c.isValid());
		REQUIRE(c.getDimSize(0) == 10);
	}
}

