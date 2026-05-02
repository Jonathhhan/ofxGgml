#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct MetricSummary {
    std::string name;
    double meanMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    int iterations = 0;
};

std::string jsonEscape(const std::string & value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double index = (static_cast<double>(values.size()) - 1.0) * p;
    const auto lower = static_cast<size_t>(std::floor(index));
    const auto upper = static_cast<size_t>(std::ceil(index));
    if (lower == upper) {
        return values[lower];
    }
    const double fraction = index - static_cast<double>(lower);
    return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

template<typename Func>
MetricSummary measureMetric(const std::string & name, int iterations, Func && func) {
    MetricSummary summary;
    summary.name = name;
    summary.iterations = iterations;

    func();

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iterations));
    for (int i = 0; i < iterations; ++i) {
        const auto start = std::chrono::high_resolution_clock::now();
        func();
        const auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    summary.meanMs = std::accumulate(samples.begin(), samples.end(), 0.0) /
        static_cast<double>(samples.size());
    summary.p50Ms = percentile(samples, 0.50);
    summary.p95Ms = percentile(samples, 0.95);
    summary.minMs = *std::min_element(samples.begin(), samples.end());
    summary.maxMs = *std::max_element(samples.begin(), samples.end());
    return summary;
}

void writeProfileOutput(
    const std::string & path,
    const std::string & profile,
    const std::string & backend,
    bool skipped,
    const std::string & reason,
    const std::vector<MetricSummary> & metrics) {
    if (path.empty()) {
        return;
    }
    std::ofstream out(path, std::ios::binary);
    out << "{\n";
    out << "  \"profile\": \"" << jsonEscape(profile) << "\",\n";
    out << "  \"backend\": \"" << jsonEscape(backend) << "\",\n";
    out << "  \"skipped\": " << (skipped ? "true" : "false");
    if (!reason.empty()) {
        out << ",\n  \"reason\": \"" << jsonEscape(reason) << "\"";
    }
    if (!metrics.empty()) {
        out << ",\n  \"metrics\": {\n";
        for (size_t i = 0; i < metrics.size(); ++i) {
            const auto & metric = metrics[i];
            out << "    \"" << jsonEscape(metric.name) << "\": {\n";
            out << "      \"mean_ms\": " << std::fixed << std::setprecision(6) << metric.meanMs << ",\n";
            out << "      \"p50_ms\": " << metric.p50Ms << ",\n";
            out << "      \"p95_ms\": " << metric.p95Ms << ",\n";
            out << "      \"min_ms\": " << metric.minMs << ",\n";
            out << "      \"max_ms\": " << metric.maxMs << ",\n";
            out << "      \"iterations\": " << metric.iterations << "\n";
            out << "    }" << (i + 1 < metrics.size() ? "," : "") << "\n";
        }
        out << "  }\n";
    } else {
        out << "\n";
    }
    out << "}\n";
}

std::string getEnvOrDefault(const char * name, const std::string & fallback) {
    const char * value = std::getenv(name);
    return value != nullptr ? std::string(value) : fallback;
}

std::vector<MetricSummary> runPerfMetrics(ofxGgml & ggml) {
    std::vector<MetricSummary> metrics;

    metrics.push_back(measureMetric("graph_alloc_p50_ms", 7, [&]() {
        ofxGgmlGraph graph;
        auto a = graph.newTensor2d(ofxGgmlType::F32, 128, 128);
        auto b = graph.newTensor2d(ofxGgmlType::F32, 128, 128);
        graph.setInput(a);
        graph.setInput(b);
        auto c = graph.matMul(a, b);
        graph.setOutput(c);
        graph.build(c);
        const auto allocResult = ggml.allocGraph(graph);
        if (!allocResult.isOk()) {
            throw std::runtime_error("allocGraph failed");
        }
    }));

    ofxGgmlGraph computeGraph;
    auto lhs = computeGraph.newTensor2d(ofxGgmlType::F32, 128, 128);
    auto rhs = computeGraph.newTensor2d(ofxGgmlType::F32, 128, 128);
    computeGraph.setInput(lhs);
    computeGraph.setInput(rhs);
    auto product = computeGraph.matMul(lhs, rhs);
    computeGraph.setOutput(product);
    computeGraph.build(product);
    REQUIRE(ggml.allocGraph(computeGraph).isOk());
    std::vector<float> dense(128 * 128, 1.0f);
    ggml.setTensorData(lhs, dense.data(), dense.size() * sizeof(float));
    ggml.setTensorData(rhs, dense.data(), dense.size() * sizeof(float));

    metrics.push_back(measureMetric("graph_compute_p50_ms", 9, [&]() {
        const auto computeResult = ggml.computeGraph(computeGraph);
        if (!computeResult.success) {
            throw std::runtime_error("computeGraph failed");
        }
    }));

    ofxGgmlGraph asyncGraph;
    auto input = asyncGraph.newTensor1d(ofxGgmlType::F32, 4096);
    asyncGraph.setInput(input);
    auto squared = asyncGraph.sqr(input);
    auto rooted = asyncGraph.sqrt(squared);
    asyncGraph.setOutput(rooted);
    asyncGraph.build(rooted);
    REQUIRE(ggml.allocGraph(asyncGraph).isOk());
    std::vector<float> asyncData(4096, 2.0f);
    ggml.setTensorData(input, asyncData.data(), asyncData.size() * sizeof(float));

    metrics.push_back(measureMetric("graph_async_p50_ms", 9, [&]() {
        const auto asyncResult = ggml.computeGraphAsync(asyncGraph);
        if (!asyncResult.success) {
            throw std::runtime_error("computeGraphAsync failed");
        }
        const auto syncResult = ggml.synchronize();
        if (!syncResult.success) {
            throw std::runtime_error("synchronize failed");
        }
    }));

    return metrics;
}

} // namespace

TEST_CASE("Performance profile emits deterministic regression metrics", "[benchmark][performance_profile]") {
    const std::string profile = getEnvOrDefault("OFXGGML_PERF_PROFILE", "cpu");
    const std::string outputPath = getEnvOrDefault("OFXGGML_PERF_OUTPUT", "");

    ofxGgml ggml;
    ofxGgmlSettings settings;
    settings.threads = 1;
    settings.graphSize = 4096;
    if (profile == "cpu") {
        settings.preferredBackendName = "CPU";
    } else if (profile == "gpu") {
        settings.preferredBackend = ofxGgmlBackendType::Gpu;
    }

    const auto setupResult = ggml.setup(settings);
    REQUIRE(setupResult.isOk());

    const std::string backendName = ggml.getBackendName();
    if (profile == "gpu" && backendName.find("CPU") == 0) {
        writeProfileOutput(
            outputPath,
            profile,
            backendName,
            true,
            "No non-CPU backend available on this runner.",
            {});
        SUCCEED("GPU performance profile skipped because only CPU backend is available.");
        return;
    }

    const auto metrics = runPerfMetrics(ggml);
    for (const auto & metric : metrics) {
        INFO(metric.name << " p50=" << metric.p50Ms << " p95=" << metric.p95Ms);
        REQUIRE(metric.meanMs > 0.0);
    }

    writeProfileOutput(outputPath, profile, backendName, false, "", metrics);
}
