#include "ofApp.h"

#include <array>
#include <utility>
#include <sstream>

namespace {

const std::array<std::pair<ofxGgmlBackend, const char *>, 6> kBackends = {{
	{ofxGgmlBackend::Auto, "Auto"},
	{ofxGgmlBackend::CPU, "CPU"},
	{ofxGgmlBackend::CUDA, "CUDA"},
	{ofxGgmlBackend::Vulkan, "Vulkan"},
	{ofxGgmlBackend::Metal, "Metal"},
	{ofxGgmlBackend::OpenCL, "OpenCL"}
}};

const char * backendLabel(ofxGgmlBackend backend) {
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
std::string formatDurationMs(float ms) {
	std::ostringstream stream;
	stream.setf(std::ios::fixed);
	stream.precision(3);
	stream << ms << " ms";
	return stream.str();
}

} // namespace

void ofApp::configureRuntime(ofxGgmlBackend backend) {
	ofxGgmlRuntimeSettings settings;
	settings.preferredBackend = backend;
	settings.allowCpuFallback = true;

	auto result = runtime.setup(settings);
	lines.clear();
	lines.push_back("ofxGgml rewrite main");
	if (result) {
		lines.push_back("runtime ready: " + runtime.getBackendName());
	} else {
		lines.push_back("runtime error: " + result.error().message);
	}
	lines.push_back("preferred backend: " + std::string(backendLabel(backend)));
	lines.push_back("devices:");
	for (const auto & device : runtime.getDevices()) {
		lines.push_back(std::string("  ") + backendLabel(device.backend) + ": " + device.name + formatBytes(device.memoryBytes));
	}
	if (runtime.isReady()) {
		lines.push_back("press Run to execute the compute graph");
	} else {
		lines.push_back("runtime not ready - adjust backend and run again");
	}
	lines.push_back("legacy-full keeps the previous broad framework.");
}

void ofApp::runComputation() {
	lines.push_back("");
	lines.push_back("running");
	if (!runtime.isReady()) {
		lines.push_back("run skipped: runtime not ready");
		return;
	}

	graph = ofxGgmlGraph();
	ofxGgmlTensor a = graph.tensor1d(ofxGgmlType::F32, 4);
	ofxGgmlTensor b = graph.tensor1d(ofxGgmlType::F32, 4);
	ofxGgmlTensor sum = graph.add(a, b);
	graph.build(sum);

	std::array<float, 4> left { 1.0f, 2.0f, 3.0f, 4.0f };
	std::array<float, 4> right { 10.0f, 20.0f, 30.0f, 40.0f };
	std::array<float, 4> output {};

	auto allocateResult = runtime.allocate(graph);
	if (!allocateResult) {
		lines.push_back("allocate error: " + allocateResult.error().message);
		return;
	}
	auto setLeft = runtime.setData(a, left.data(), sizeof(left));
	if (!setLeft) {
		lines.push_back("setData(left) error: " + setLeft.error().message);
		return;
	}
	auto setRight = runtime.setData(b, right.data(), sizeof(right));
	if (!setRight) {
		lines.push_back("setData(right) error: " + setRight.error().message);
		return;
	}
	ofxGgmlComputeResult compute = runtime.compute(graph);
	if (!compute) {
		lines.push_back("compute error: " + (compute.error.empty() ? "graph compute failed" : compute.error));
		return;
	}
	auto readResult = runtime.getData(sum, output.data(), sizeof(output));
	if (!readResult) {
		lines.push_back("read error: " + readResult.error().message);
		return;
	}

	lastComputeTime = formatDurationMs(compute.elapsedMs);
	lines.push_back("graph: [" + formatVector(left) + "] + [" + formatVector(right) + "]");
	lines.push_back("result: [" + formatVector(output) + "]");
	lines.push_back("calculation time: " + lastComputeTime);
}

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml rewrite");
	ofBackground(12);
	gui.setup(nullptr, false);
	configureRuntime(ofxGgmlBackend::Auto);
	selectedBackendIndex = 0;
	lastComputeTime = "--";
}

void ofApp::draw() {
	ofBackground(12);

	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_Once);
	if (ImGui::Begin("ofxGgml Simple Example")) {
	ImGui::TextUnformatted("Runtime");
	ImGui::Text("backend:");
		if (ImGui::BeginCombo("##backend", backendLabel(kBackends[selectedBackendIndex].first))) {
			for (std::size_t index = 0; index < kBackends.size(); ++index) {
				const auto & backend = kBackends[index];
				const bool selected = static_cast<int>(index) == selectedBackendIndex;
				if (ImGui::Selectable(backend.second, selected)) {
					selectedBackendIndex = static_cast<int>(index);
					configureRuntime(backend.first);
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("Run")) {
			runComputation();
		}
		ImGui::SameLine();
		ImGui::Text("calc time: %s", lastComputeTime.c_str());
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
