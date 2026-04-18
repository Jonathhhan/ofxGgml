#pragma once

#include "ofMain.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

/// Performance metrics tracking for inference operations.
///
/// Tracks tokens/sec, memory usage, cache hit rates, latency, and custom counters.
/// Thread-safe for concurrent updates from multiple inference calls.
///
/// Example usage:
/// ```cpp
/// auto& metrics = ofxGgmlMetrics::getInstance();
/// metrics.recordInferenceStart("myModel");
/// // ... do inference ...
/// metrics.recordInferenceEnd("myModel", 100); // 100 tokens generated
/// metrics.recordTokens("myModel", 100, elapsedMs);
/// std::cout << metrics.getSummary() << std::endl;
/// ```
class ofxGgmlMetrics {
public:
	struct InferenceStats {
		size_t totalCalls = 0;
		size_t successfulCalls = 0;
		size_t failedCalls = 0;
		size_t totalTokens = 0;
		double totalTimeMs = 0.0;
		double minTimeMs = std::numeric_limits<double>::max();
		double maxTimeMs = 0.0;
		size_t activeCalls = 0;
		uint64_t lastCallTimestamp = 0;
	};

	struct CacheStats {
		size_t hits = 0;
		size_t misses = 0;
		size_t evictions = 0;
	};

	struct MemoryStats {
		size_t currentBytes = 0;
		size_t peakBytes = 0;
		size_t totalAllocations = 0;
		size_t totalDeallocations = 0;
	};

	/// Get singleton instance.
	static ofxGgmlMetrics& getInstance() {
		static ofxGgmlMetrics instance;
		return instance;
	}

	/// Record inference start.
	void recordInferenceStart(const std::string& modelName) {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& stats = m_inferenceStats[modelName];
		stats.totalCalls++;
		stats.activeCalls++;
		stats.lastCallTimestamp = getCurrentTimestamp();
	}

	/// Record inference end with success/failure.
	void recordInferenceEnd(const std::string& modelName, size_t tokensGenerated, double elapsedMs, bool success = true) {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& stats = m_inferenceStats[modelName];
		if (stats.activeCalls > 0) {
			stats.activeCalls--;
		}
		if (success) {
			stats.successfulCalls++;
			stats.totalTokens += tokensGenerated;
			stats.totalTimeMs += elapsedMs;
			stats.minTimeMs = std::min(stats.minTimeMs, elapsedMs);
			stats.maxTimeMs = std::max(stats.maxTimeMs, elapsedMs);
		} else {
			stats.failedCalls++;
		}
	}

	/// Record tokens generated and time taken.
	void recordTokens(const std::string& modelName, size_t tokens, double elapsedMs) {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& stats = m_inferenceStats[modelName];
		stats.totalTokens += tokens;
		stats.totalTimeMs += elapsedMs;
	}

	/// Record cache hit.
	void recordCacheHit(const std::string& cacheName = "default") {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_cacheStats[cacheName].hits++;
	}

	/// Record cache miss.
	void recordCacheMiss(const std::string& cacheName = "default") {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_cacheStats[cacheName].misses++;
	}

	/// Record cache eviction.
	void recordCacheEviction(const std::string& cacheName = "default") {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_cacheStats[cacheName].evictions++;
	}

	/// Record memory allocation.
	void recordAllocation(size_t bytes, const std::string& category = "default") {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& stats = m_memoryStats[category];
		stats.currentBytes += bytes;
		stats.peakBytes = std::max(stats.peakBytes, stats.currentBytes);
		stats.totalAllocations++;
	}

	/// Record memory deallocation.
	void recordDeallocation(size_t bytes, const std::string& category = "default") {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& stats = m_memoryStats[category];
		if (stats.currentBytes >= bytes) {
			stats.currentBytes -= bytes;
		} else {
			stats.currentBytes = 0;
		}
		stats.totalDeallocations++;
	}

	/// Increment a custom counter.
	void incrementCounter(const std::string& name, size_t amount = 1) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_counters[name] += amount;
	}

	/// Set a custom gauge value.
	void setGauge(const std::string& name, double value) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_gauges[name] = value;
	}

	/// Record a custom timing.
	void recordTiming(const std::string& name, double milliseconds) {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& timings = m_timings[name];
		timings.push_back(milliseconds);
		// Keep only last 1000 samples to avoid unbounded growth
		if (timings.size() > 1000) {
			timings.erase(timings.begin());
		}
	}

	/// Get inference stats for a model.
	InferenceStats getInferenceStats(const std::string& modelName) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_inferenceStats.find(modelName);
		return (it != m_inferenceStats.end()) ? it->second : InferenceStats{};
	}

	/// Get cache stats.
	CacheStats getCacheStats(const std::string& cacheName = "default") const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_cacheStats.find(cacheName);
		return (it != m_cacheStats.end()) ? it->second : CacheStats{};
	}

	/// Get memory stats.
	MemoryStats getMemoryStats(const std::string& category = "default") const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_memoryStats.find(category);
		return (it != m_memoryStats.end()) ? it->second : MemoryStats{};
	}

	/// Get counter value.
	size_t getCounter(const std::string& name) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_counters.find(name);
		return (it != m_counters.end()) ? it->second : 0;
	}

	/// Get gauge value.
	double getGauge(const std::string& name) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_gauges.find(name);
		return (it != m_gauges.end()) ? it->second : 0.0;
	}

	/// Get average tokens per second for a model.
	double getAverageTokensPerSecond(const std::string& modelName) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_inferenceStats.find(modelName);
		if (it != m_inferenceStats.end() && it->second.totalTimeMs > 0.0) {
			return (it->second.totalTokens * 1000.0) / it->second.totalTimeMs;
		}
		return 0.0;
	}

	/// Get cache hit rate (0.0 to 1.0).
	double getCacheHitRate(const std::string& cacheName = "default") const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_cacheStats.find(cacheName);
		if (it != m_cacheStats.end()) {
			size_t total = it->second.hits + it->second.misses;
			return (total > 0) ? (static_cast<double>(it->second.hits) / total) : 0.0;
		}
		return 0.0;
	}

	/// Get formatted summary of all metrics.
	std::string getSummary() const;

	/// Reset all metrics.
	void reset() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_inferenceStats.clear();
		m_cacheStats.clear();
		m_memoryStats.clear();
		m_counters.clear();
		m_gauges.clear();
		m_timings.clear();
	}

	/// Reset metrics for a specific model.
	void resetModel(const std::string& modelName) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_inferenceStats.erase(modelName);
	}

private:
	ofxGgmlMetrics() = default;
	~ofxGgmlMetrics() = default;
	ofxGgmlMetrics(const ofxGgmlMetrics&) = delete;
	ofxGgmlMetrics& operator=(const ofxGgmlMetrics&) = delete;

	static uint64_t getCurrentTimestamp() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	mutable std::mutex m_mutex;
	std::map<std::string, InferenceStats> m_inferenceStats;
	std::map<std::string, CacheStats> m_cacheStats;
	std::map<std::string, MemoryStats> m_memoryStats;
	std::map<std::string, size_t> m_counters;
	std::map<std::string, double> m_gauges;
	std::map<std::string, std::vector<double>> m_timings;
};
