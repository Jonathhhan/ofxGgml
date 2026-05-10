#include "ofApp.h"

#include <array>
#include <sstream>

namespace {

std::string backendLabel(ofxGgmlBackend backend) {
	return ofxGgmlGetBackendName(backend);
}

std::string formatBytes(std::size_t bytes) {
	if (bytes == 0) {
		return "";
	}
	const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
	std::ostringstream stream;
	stream.setf(std::ios::fixed);
	stream.precision(1);
	stream << " (" << gib << " GiB)";
	return stream.str();
}

std::string formatVector(const std::array<float, 4> & values) {
	std::ostringstream stream;
	stream << values[0] << ", " << values[1] << ", " << values[2] << ", " << values[3];
	return stream.str();
}

} // namespace

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml rewrite");
	ofBackground(12);
	gui.setup(nullptr, false);

	auto result = runtime.setup();
	lines.clear();
	lines.push_back("ofxGgml rewrite main");
	lines.push_back(result ? "runtime ready: " + runtime.getBackendName() : "runtime error: " + result.error().message);
	lines.push_back("preferred backend: Auto");
	lines.push_back("devices:");
	for (const auto & device : runtime.listDevices()) {
		lines.push_back("  " + backendLabel(device.backend) + ": " + device.name + formatBytes(device.memoryBytes));
	}
	if (result) {
		graph = ofxGgmlGraph();
		ofxGgmlTensor a = graph.tensor1d(ofxGgmlType::F32, 4);
		ofxGgmlTensor b = graph.tensor1d(ofxGgmlType::F32, 4);
		ofxGgmlTensor sum = graph.add(a, b);
		graph.build(sum);

		std::array<float, 4> left { 1.0f, 2.0f, 3.0f, 4.0f };
		std::array<float, 4> right { 10.0f, 20.0f, 30.0f, 40.0f };
		std::array<float, 4> output {};
		std::string graphError;

		auto allocateResult = runtime.allocate(graph);
		if (!allocateResult) {
			graphError = allocateResult.error().message;
		}
		auto setLeft = graphError.empty() ? runtime.setData(a, left.data(), sizeof(left)) : ofxGgmlResult<void>::failure(graphError);
		if (!setLeft && graphError.empty()) {
			graphError = setLeft.error().message;
		}
		auto setRight = graphError.empty() ? runtime.setData(b, right.data(), sizeof(right)) : ofxGgmlResult<void>::failure(graphError);
		if (!setRight && graphError.empty()) {
			graphError = setRight.error().message;
		}
		ofxGgmlComputeResult compute = graphError.empty() ? runtime.compute(graph) : ofxGgmlComputeResult {};
		if (!compute && graphError.empty()) {
			graphError = compute.error.empty() ? "graph compute failed" : compute.error;
		}
		auto readResult = graphError.empty()
			? runtime.getData(sum, output.data(), sizeof(output))
			: ofxGgmlResult<void>::failure(graphError);

		if (readResult) {
			lines.push_back("graph: [" + formatVector(left) + "] + [" + formatVector(right) + "]");
			lines.push_back("result: [" + formatVector(output) + "]");
		} else {
			lines.push_back("graph error: " + readResult.error().message);
		}
	}
	lines.push_back("legacy-full keeps the previous broad framework.");
}

void ofApp::draw() {
	ofBackground(12);

	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_Once);
	if (ImGui::Begin("ofxGgml Simple Example")) {
		ImGui::TextUnformatted("Runtime");
		ImGui::Separator();
		for (const auto & line : lines) {
			if (line.rfind("runtime error:", 0) == 0 || line.rfind("graph error:", 0) == 0) {
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", line.c_str());
			} else {
				ImGui::TextWrapped("%s", line.c_str());
			}
		}
	}
	ImGui::End();
	gui.end();
	gui.draw();
}
