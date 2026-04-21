#include "ofxGgmlMetrics.h"
#include <sstream>
#include <iomanip>

std::string ofxGgmlMetrics::getSummary() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	std::ostringstream oss;

	oss << "=== ofxGgml Metrics Summary ===\n\n";

	// Inference stats
	if (!m_inferenceStats.empty()) {
		oss << "Inference Statistics:\n";
		for (const auto& [model, stats] : m_inferenceStats) {
			oss << "  Model: " << model << "\n";
			oss << "    Total calls: " << stats.totalCalls
				<< " (success: " << stats.successfulCalls
				<< ", failed: " << stats.failedCalls
				<< ", active: " << stats.activeCalls << ")\n";
			oss << "    Total tokens: " << stats.totalTokens << "\n";
			if (stats.totalTimeMs > 0.0) {
				double avgTps = (stats.totalTokens * 1000.0) / stats.totalTimeMs;
				oss << "    Avg tokens/sec: " << std::fixed << std::setprecision(2) << avgTps << "\n";
				oss << "    Total time: " << std::fixed << std::setprecision(2) << stats.totalTimeMs << " ms\n";
				if (stats.successfulCalls > 0) {
					double avgTime = stats.totalTimeMs / stats.successfulCalls;
					oss << "    Avg time/call: " << std::fixed << std::setprecision(2) << avgTime << " ms\n";
				}
				oss << "    Min/Max time: " << std::fixed << std::setprecision(2)
					<< stats.minTimeMs << " / " << stats.maxTimeMs << " ms\n";
			}
		}
		oss << "\n";
	}

	// Cache stats
	if (!m_cacheStats.empty()) {
		oss << "Cache Statistics:\n";
		for (const auto& [name, stats] : m_cacheStats) {
			size_t total = stats.hits + stats.misses;
			double hitRate = (total > 0) ? (static_cast<double>(stats.hits) / total * 100.0) : 0.0;
			oss << "  Cache: " << name << "\n";
			oss << "    Hits: " << stats.hits << ", Misses: " << stats.misses
				<< " (hit rate: " << std::fixed << std::setprecision(1) << hitRate << "%)\n";
			oss << "    Evictions: " << stats.evictions << "\n";
		}
		oss << "\n";
	}

	// Memory stats
	if (!m_memoryStats.empty()) {
		oss << "Memory Statistics:\n";
		for (const auto& [category, stats] : m_memoryStats) {
			oss << "  Category: " << category << "\n";
			oss << "    Current: " << (stats.currentBytes / 1024.0 / 1024.0) << " MB\n";
			oss << "    Peak: " << (stats.peakBytes / 1024.0 / 1024.0) << " MB\n";
			oss << "    Allocations: " << stats.totalAllocations
				<< ", Deallocations: " << stats.totalDeallocations << "\n";
		}
		oss << "\n";
	}

	// Counters
	if (!m_counters.empty()) {
		const auto stream = getStreamStats();
		if (!stream.empty()) {
			oss << "Streaming:\n";
			for (const auto & [transport, agg] : stream) {
				oss << "  " << transport
					<< " chunks=" << agg.chunks
					<< " bytes=" << agg.bytes
					<< " cancelled=" << agg.cancelled
					<< "\n";
			}
			oss << "\n";
		}

		oss << "Counters:\n";
		for (const auto& [name, value] : m_counters) {
			oss << "  " << name << ": " << value << "\n";
		}
		oss << "\n";
	}

	// Gauges
	if (!m_gauges.empty()) {
		oss << "Gauges:\n";
		for (const auto& [name, value] : m_gauges) {
			oss << "  " << name << ": " << std::fixed << std::setprecision(2) << value << "\n";
		}
		oss << "\n";
	}

	// Timings
	if (!m_timings.empty()) {
		oss << "Timings:\n";
		for (const auto& [name, samples] : m_timings) {
			if (!samples.empty()) {
				double sum = 0.0;
				double minVal = std::numeric_limits<double>::max();
				double maxVal = 0.0;
				for (double v : samples) {
					sum += v;
					minVal = std::min(minVal, v);
					maxVal = std::max(maxVal, v);
				}
				double avg = sum / samples.size();
				oss << "  " << name << ": avg=" << std::fixed << std::setprecision(2) << avg
					<< " ms, min=" << minVal << " ms, max=" << maxVal
					<< " ms (n=" << samples.size() << ")\n";
			}
		}
		oss << "\n";
	}

	return oss.str();
}
