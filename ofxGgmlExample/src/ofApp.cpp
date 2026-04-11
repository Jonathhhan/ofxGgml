#include "ofApp.h"
#include "support/ofxGgmlHelpers.h"

#include <sstream>

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml — Matrix Multiplication");
	ofBackground(30);

	// Initialize ggml with default (CPU) settings.
	ofxGgmlSettings settings;
	settings.threads = 4;
	if (!ggml.setup(settings)) {
		logLines.push_back("ERROR: failed to initialize ggml");
		return;
	}

	logLines.push_back("Backend: " + ggml.getBackendName());

	// List discovered devices.
	auto devices = ggml.listDevices();
	for (auto & d : devices) {
		logLines.push_back("  Device: " + d.name + " (" + d.description + ") "
			+ ofxGgmlHelpers::formatBytes(d.memoryTotal));
	}
	logLines.push_back("");

	// Build a computation graph: result = A * B^T
	// A is 4x2, B is 3x2 (ggml stores col-major, ggml_mul_mat does A * B^T).
	const int rowsA = 4, colsA = 2;
	const int rowsB = 3, colsB = 2;

	ofxGgmlGraph graph;
	auto a = graph.newTensor2d(ofxGgmlType::F32, colsA, rowsA);
	auto b = graph.newTensor2d(ofxGgmlType::F32, colsB, rowsB);
	a.setName("A");
	b.setName("B");
	graph.setInput(a);
	graph.setInput(b);

	auto result = graph.matMul(a, b);
	result.setName("result");
	graph.setOutput(result);
	graph.build(result);

	// Set data.
	float matA[] = { 2, 8, 5, 1, 4, 2, 8, 6 };
	float matB[] = { 10, 5, 9, 9, 5, 4 };
	ggml.setTensorData(a, matA, sizeof(matA));
	ggml.setTensorData(b, matB, sizeof(matB));

	// Compute.
	auto r = ggml.compute(graph);

	if (r.success) {
		logLines.push_back("Compute OK (" + ofToString(r.elapsedMs, 2) + " ms)");

		std::vector<float> out(static_cast<size_t>(result.getNumElements()));
		ggml.getTensorData(result, out.data(), out.size() * sizeof(float));

		// Pretty-print the result matrix.
		const int outCols = static_cast<int>(result.getDimSize(0));
		const int outRows = static_cast<int>(result.getDimSize(1));
		logLines.push_back("Result (" + ofToString(outRows) + " x " + ofToString(outCols) + "):");

		for (int row = 0; row < outRows; row++) {
			std::ostringstream line;
			line << "  [";
			for (int col = 0; col < outCols; col++) {
				if (col > 0) line << ", ";
				line << ofToString(out[static_cast<size_t>(row * outCols + col)], 1);
			}
			line << "]";
			logLines.push_back(line.str());
		}
	} else {
		logLines.push_back("Compute FAILED: " + r.error);
	}
}

void ofApp::draw() {
	ofSetColor(220);
	float y = 30;
	for (auto & line : logLines) {
		ofDrawBitmapString(line, 20, y);
		y += 16;
	}
}
