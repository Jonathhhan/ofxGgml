#include "test_harness.h"
#include "ggml-backend.h"
#include "../src/core/ofxGgmlRuntime.h"
#include "../src/compute/ofxGgmlGraph.h"

#include <array>

OFXGGML_TEST(runtime_starts_uninitialized) {
	ofxGgmlRuntime runtime;

	OFXGGML_REQUIRE(!runtime.isReady());
	OFXGGML_REQUIRE(runtime.state() == ofxGgmlRuntimeState::Uninitialized);
	OFXGGML_REQUIRE(runtime.backendName().empty());
}

OFXGGML_TEST(runtime_setup_auto_falls_back_to_cpu_backend) {
	ofxGgmlRuntime runtime;
	auto result = runtime.setup();

	OFXGGML_REQUIRE(result.isOk());
	OFXGGML_REQUIRE(runtime.isReady());
	OFXGGML_REQUIRE(runtime.state() == ofxGgmlRuntimeState::Ready);
	OFXGGML_REQUIRE(runtime.backendName() == "CPU");

	auto devices = runtime.listDevices();
	OFXGGML_REQUIRE(devices.size() == 1);
	OFXGGML_REQUIRE(devices[0].backend == ofxGgmlBackend::Cpu);
	OFXGGML_REQUIRE(devices[0].available);
}

OFXGGML_TEST(runtime_setup_explicit_cpu_backend) {
	ofxGgmlRuntime runtime;
	ofxGgmlRuntimeSettings settings;
	settings.preferredBackend = ofxGgmlBackend::Cpu;

	auto result = runtime.setup(settings);

	OFXGGML_REQUIRE(result.isOk());
	OFXGGML_REQUIRE(runtime.backendName() == "CPU");
}

OFXGGML_TEST(runtime_requested_gpu_falls_back_or_errors) {
	ofxGgmlRuntime fallbackRuntime;
	ofxGgmlRuntimeSettings fallbackSettings;
	fallbackSettings.preferredBackend = ofxGgmlBackend::Cuda;
	fallbackSettings.allowCpuFallback = true;

	OFXGGML_REQUIRE(fallbackRuntime.setup(fallbackSettings).isOk());
	OFXGGML_REQUIRE(fallbackRuntime.backendName() == "CPU");

	ofxGgmlRuntime strictRuntime;
	ofxGgmlRuntimeSettings strictSettings;
	strictSettings.preferredBackend = ofxGgmlBackend::Cuda;
	strictSettings.allowCpuFallback = false;

	OFXGGML_REQUIRE(strictRuntime.setup(strictSettings).isError());
	OFXGGML_REQUIRE(strictRuntime.state() == ofxGgmlRuntimeState::Error);
}

OFXGGML_TEST(runtime_allocate_requires_ready_built_graph) {
	ofxGgmlRuntime runtime;
	ofxGgmlGraph graph;

	OFXGGML_REQUIRE(runtime.allocate(graph).isError());
	OFXGGML_REQUIRE(runtime.setup().isOk());
	OFXGGML_REQUIRE(runtime.allocate(graph).isError());

	ofxGgmlTensor a = graph.tensor1d(ofxGgmlType::F32, 4);
	ofxGgmlTensor b = graph.tensor1d(ofxGgmlType::F32, 4);
	graph.build(graph.add(a, b));

	OFXGGML_REQUIRE(runtime.allocate(graph).isOk());
}

OFXGGML_TEST(runtime_data_transfer_validates_inputs) {
	ofxGgmlRuntime runtime;
	ofxGgmlGraph graph;
	ofxGgmlTensor tensor = graph.tensor1d(ofxGgmlType::F32, 4);
	graph.build(tensor);
	std::array<float, 4> values { 1.0f, 2.0f, 3.0f, 4.0f };

	OFXGGML_REQUIRE(runtime.setData(tensor, values.data(), sizeof(values)).isError());
	OFXGGML_REQUIRE(runtime.setup().isOk());
	OFXGGML_REQUIRE(runtime.setData(tensor, values.data(), sizeof(values)).isError());
	OFXGGML_REQUIRE(runtime.allocate(graph).isOk());
	OFXGGML_REQUIRE(runtime.setData({}, values.data(), sizeof(values)).isError());
	OFXGGML_REQUIRE(runtime.setData(tensor, nullptr, sizeof(values)).isError());
	OFXGGML_REQUIRE(runtime.setData(tensor, values.data(), sizeof(float)).isError());
	OFXGGML_REQUIRE(runtime.setData(tensor, values.data(), sizeof(values)).isOk());

	std::array<float, 4> copy {};
	OFXGGML_REQUIRE(runtime.getData(tensor, copy.data(), sizeof(copy)).isOk());
	OFXGGML_REQUIRE(copy == values);
}

OFXGGML_TEST(runtime_computes_cpu_graph_end_to_end) {
	ofxGgmlRuntime runtime;
	ofxGgmlGraph graph;
	ofxGgmlTensor a = graph.tensor1d(ofxGgmlType::F32, 4);
	ofxGgmlTensor b = graph.tensor1d(ofxGgmlType::F32, 4);
	ofxGgmlTensor sum = graph.add(a, b);
	graph.build(sum);

	std::array<float, 4> left { 1.0f, 2.0f, 3.0f, 4.0f };
	std::array<float, 4> right { 10.0f, 20.0f, 30.0f, 40.0f };
	std::array<float, 4> output {};

	OFXGGML_REQUIRE(runtime.setup().isOk());
	OFXGGML_REQUIRE(runtime.compute(graph).success == false);
	OFXGGML_REQUIRE(runtime.allocate(graph).isOk());
	OFXGGML_REQUIRE(runtime.setData(a, left.data(), sizeof(left)).isOk());
	OFXGGML_REQUIRE(runtime.setData(b, right.data(), sizeof(right)).isOk());

	ofxGgmlComputeResult compute = runtime.compute(graph);
	OFXGGML_REQUIRE(compute.success);
	OFXGGML_REQUIRE(compute.error.empty());

	OFXGGML_REQUIRE(runtime.getData(sum, output.data(), sizeof(output)).isOk());
	OFXGGML_REQUIRE(output[0] == 11.0f);
	OFXGGML_REQUIRE(output[1] == 22.0f);
	OFXGGML_REQUIRE(output[2] == 33.0f);
	OFXGGML_REQUIRE(output[3] == 44.0f);
}

OFXGGML_TEST(runtime_close_resets_state) {
	ofxGgmlRuntime runtime;

	OFXGGML_REQUIRE(runtime.setup().isOk());
	runtime.close();

	OFXGGML_REQUIRE(!runtime.isReady());
	OFXGGML_REQUIRE(runtime.state() == ofxGgmlRuntimeState::Uninitialized);
	OFXGGML_REQUIRE(runtime.backendName().empty());
}

OFXGGML_TEST(runtime_move_assignment_releases_replaced_handles) {
	const int backendCount = ggml_test_backend_live_count();
	const int bufferCount = ggml_test_buffer_live_count();

	{
		ofxGgmlRuntime target;
		ofxGgmlGraph graph;
		ofxGgmlTensor tensor = graph.tensor1d(ofxGgmlType::F32, 4);
		graph.build(tensor);

		OFXGGML_REQUIRE(target.setup().isOk());
		OFXGGML_REQUIRE(target.allocate(graph).isOk());
		OFXGGML_REQUIRE(ggml_test_backend_live_count() == backendCount + 1);
		OFXGGML_REQUIRE(ggml_test_buffer_live_count() == bufferCount + 1);

		ofxGgmlRuntime source;
		OFXGGML_REQUIRE(source.setup().isOk());
		OFXGGML_REQUIRE(ggml_test_backend_live_count() == backendCount + 2);

		target = std::move(source);

		OFXGGML_REQUIRE(target.isReady());
		OFXGGML_REQUIRE(ggml_test_backend_live_count() == backendCount + 1);
		OFXGGML_REQUIRE(ggml_test_buffer_live_count() == bufferCount);
	}

	OFXGGML_REQUIRE(ggml_test_backend_live_count() == backendCount);
	OFXGGML_REQUIRE(ggml_test_buffer_live_count() == bufferCount);
}
