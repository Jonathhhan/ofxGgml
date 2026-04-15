#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("Graph creation and lifecycle", "[graph]") {
	SECTION("Default construction") {
		ofxGgmlGraph graph;
		// Graph should be usable immediately
		auto t = graph.newTensor1d(ofxGgmlType::F32, 5);
		REQUIRE(t.isValid());
	}

	SECTION("Custom max nodes") {
		ofxGgmlGraph graph(1024);
		auto t = graph.newTensor2d(ofxGgmlType::F32, 3, 3);
		REQUIRE(t.isValid());
	}

	SECTION("Reset allows reuse") {
		ofxGgmlGraph graph;
		auto t1 = graph.newTensor1d(ofxGgmlType::F32, 10);
		REQUIRE(t1.isValid());

		graph.reset();
		auto t2 = graph.newTensor1d(ofxGgmlType::F32, 5);
		REQUIRE(t2.isValid());
	}
}

TEST_CASE("Graph tensor marking", "[graph]") {
	ofxGgmlGraph graph;
	auto t = graph.newTensor2d(ofxGgmlType::F32, 3, 3);

	SECTION("Set input") {
		// Should not throw
		REQUIRE_NOTHROW(graph.setInput(t));
	}

	SECTION("Set output") {
		REQUIRE_NOTHROW(graph.setOutput(t));
	}

	SECTION("Set param") {
		REQUIRE_NOTHROW(graph.setParam(t));
	}

	SECTION("Marking invalid tensor is safe") {
		ofxGgmlTensor invalid;
		REQUIRE_NOTHROW(graph.setInput(invalid));
		REQUIRE_NOTHROW(graph.setOutput(invalid));
		REQUIRE_NOTHROW(graph.setParam(invalid));
	}
}

TEST_CASE("Matrix operations", "[graph][ops]") {
	ofxGgmlGraph graph;

	SECTION("Matrix multiplication dimensions") {
		// matMul(a, b) computes a * b^T
		auto a = graph.newTensor2d(ofxGgmlType::F32, 4, 3);  // 4 cols, 3 rows
		auto b = graph.newTensor2d(ofxGgmlType::F32, 4, 2);  // 4 cols, 2 rows
		auto c = graph.matMul(a, b);

		REQUIRE(c.isValid());
		// Result should be 3 x 2 (rows of a x rows of b)
		REQUIRE(c.getDimSize(0) == 3);
		REQUIRE(c.getDimSize(1) == 2);
	}

	SECTION("Transpose") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 3, 5);
		auto t = graph.transpose(a);

		REQUIRE(t.isValid());
		// Dimensions should be swapped
		REQUIRE(t.getDimSize(0) == 5);
		REQUIRE(t.getDimSize(1) == 3);
	}
}

TEST_CASE("Reduction operations", "[graph][ops]") {
	ofxGgmlGraph graph;

	SECTION("Sum") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 3, 4);
		auto s = graph.sum(a);

		REQUIRE(s.isValid());
		// Sum reduces to scalar
		REQUIRE(s.getNumElements() == 1);
	}

	SECTION("Mean") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 5);
		auto m = graph.mean(a);

		REQUIRE(m.isValid());
		REQUIRE(m.getNumElements() > 0);
	}

	SECTION("Sum rows") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 4, 3);
		auto s = graph.sumRows(a);

		REQUIRE(s.isValid());
		// Sum rows reduces second dimension
	}

	SECTION("Argmax") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 10);
		auto idx = graph.argmax(a);

		REQUIRE(idx.isValid());
	}
}

TEST_CASE("Activation functions", "[graph][ops]") {
	ofxGgmlGraph graph;

	SECTION("ReLU") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 3, 3);
		auto r = graph.relu(a);

		REQUIRE(r.isValid());
		REQUIRE(r.getDimSize(0) == 3);
		REQUIRE(r.getDimSize(1) == 3);
	}

	SECTION("GELU") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 8);
		auto g = graph.gelu(a);

		REQUIRE(g.isValid());
		REQUIRE(g.getDimSize(0) == 8);
	}

	SECTION("SiLU") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 4, 4);
		auto s = graph.silu(a);

		REQUIRE(s.isValid());
	}

	SECTION("Sigmoid") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 5);
		auto s = graph.sigmoid(a);

		REQUIRE(s.isValid());
		REQUIRE(s.getDimSize(0) == 5);
	}

	SECTION("Tanh") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 6);
		auto t = graph.tanh(a);

		REQUIRE(t.isValid());
	}

	SECTION("Softmax") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 10);
		auto s = graph.softmax(a);

		REQUIRE(s.isValid());
		REQUIRE(s.getDimSize(0) == 10);
	}
}

TEST_CASE("Normalization operations", "[graph][ops]") {
	ofxGgmlGraph graph;

	SECTION("Norm") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 3, 3);
		auto n = graph.norm(a);

		REQUIRE(n.isValid());
	}

	SECTION("RMS Norm") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 8);
		auto r = graph.rmsNorm(a);

		REQUIRE(r.isValid());
		REQUIRE(r.getDimSize(0) == 8);
	}

	SECTION("RMS Norm with epsilon") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 4, 4);
		auto r = graph.rmsNorm(a, 1e-6f);

		REQUIRE(r.isValid());
	}
}

TEST_CASE("Reshape operations", "[graph][ops]") {
	ofxGgmlGraph graph;

	SECTION("Reshape 1D to 2D") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 12);
		auto r = graph.reshape2d(a, 3, 4);

		REQUIRE(r.isValid());
		REQUIRE(r.getDimSize(0) == 3);
		REQUIRE(r.getDimSize(1) == 4);
	}

	SECTION("Reshape to 3D") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 24);
		auto r = graph.reshape3d(a, 2, 3, 4);

		REQUIRE(r.isValid());
		REQUIRE(r.getDimSize(0) == 2);
		REQUIRE(r.getDimSize(1) == 3);
		REQUIRE(r.getDimSize(2) == 4);
	}
}

TEST_CASE("Graph building", "[graph]") {
	ofxGgmlGraph graph;

	SECTION("Build with single output") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		graph.setInput(a);
		graph.setInput(b);
		auto c = graph.add(a, b);
		graph.setOutput(c);

		REQUIRE_NOTHROW(graph.build(c));
	}

	SECTION("Build with multiple outputs") {
		auto a = graph.newTensor1d(ofxGgmlType::F32, 5);
		graph.setInput(a);
		auto b = graph.relu(a);
		auto c = graph.sqrt(a);
		graph.setOutput(b);
		graph.setOutput(c);

		std::vector<ofxGgmlTensor> outputs = {b, c};
		REQUIRE_NOTHROW(graph.build(outputs));
	}

	SECTION("Get node count after build") {
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto b = graph.sqr(a);
		graph.build(b);

		int nodeCount = graph.getNumNodes();
		REQUIRE(nodeCount >= 0);
	}
}

TEST_CASE("Convolution operations", "[graph][ops]") {
	ofxGgmlGraph graph;

	SECTION("Conv1D") {
		auto input = graph.newTensor2d(ofxGgmlType::F32, 8, 1);  // 8 elements, 1 channel
		auto kernel = graph.newTensor2d(ofxGgmlType::F32, 3, 1); // 3 kernel size
		auto output = graph.conv1d(input, kernel, 1, 0, 1);

		REQUIRE(output.isValid());
	}

	SECTION("Pool1D") {
		auto input = graph.newTensor2d(ofxGgmlType::F32, 8, 1);
		auto output = graph.pool1d(input, 2, 2, 0, ofxGgmlPoolType::Max);

		REQUIRE(output.isValid());
	}

	SECTION("Pool2D") {
		auto input = graph.newTensor3d(ofxGgmlType::F32, 4, 4, 1);
		auto output = graph.pool2d(input, 2, 2, 0, ofxGgmlPoolType::Avg);

		REQUIRE(output.isValid());
	}

	SECTION("Upscale") {
		auto input = graph.newTensor3d(ofxGgmlType::F32, 4, 4, 1);
		auto output = graph.upscale(input, 2);

		REQUIRE(output.isValid());
	}
}
