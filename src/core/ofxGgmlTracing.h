#pragma once

#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <mutex>
#include <memory>
#include <optional>
#include <sstream>
#include <iomanip>

/// Generate a unique trace ID (128-bit represented as hex string)
inline std::string generateTraceId() {
	static std::random_device rd;
	static std::mt19937_64 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;

	uint64_t high = dist(gen);
	uint64_t low = dist(gen);

	std::ostringstream oss;
	oss << std::hex << std::setfill('0') << std::setw(16) << high << std::setw(16) << low;
	return oss.str();
}

/// Generate a unique span ID (64-bit represented as hex string)
inline std::string generateSpanId() {
	static std::random_device rd;
	static std::mt19937_64 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;

	uint64_t id = dist(gen);
	std::ostringstream oss;
	oss << std::hex << std::setfill('0') << std::setw(16) << id;
	return oss.str();
}

/// Span status
enum class SpanStatus {
	Ok,
	Error,
	Unset
};

/// A single span in a distributed trace.
///
/// Spans represent a unit of work (e.g., "generate text", "load model", "embed prompt").
/// They can be nested to show hierarchical relationships.
///
struct ofxGgmlSpan {
	std::string traceId;
	std::string spanId;
	std::string parentSpanId; // Empty if root span
	std::string name;
	SpanStatus status = SpanStatus::Unset;
	std::chrono::system_clock::time_point startTime;
	std::chrono::system_clock::time_point endTime;
	std::map<std::string, std::string> attributes;
	std::vector<std::pair<std::chrono::system_clock::time_point, std::string>> events;

	/// Duration in milliseconds
	float durationMs() const {
		auto duration = endTime - startTime;
		return std::chrono::duration<float, std::milli>(duration).count();
	}

	/// Check if span has ended
	bool isComplete() const {
		return endTime > startTime;
	}

	/// Format as JSON for export
	std::string toJson() const {
		std::ostringstream json;
		json << "{";
		json << "\"traceId\":\"" << traceId << "\",";
		json << "\"spanId\":\"" << spanId << "\",";
		if (!parentSpanId.empty()) {
			json << "\"parentSpanId\":\"" << parentSpanId << "\",";
		}
		json << "\"name\":\"" << escapeJson(name) << "\",";
		json << "\"status\":\"";
		switch (status) {
			case SpanStatus::Ok: json << "OK"; break;
			case SpanStatus::Error: json << "ERROR"; break;
			case SpanStatus::Unset: json << "UNSET"; break;
		}
		json << "\",";
		json << "\"startTime\":" << toEpochMicros(startTime) << ",";
		json << "\"endTime\":" << toEpochMicros(endTime) << ",";
		json << "\"durationMs\":" << durationMs();

		if (!attributes.empty()) {
			json << ",\"attributes\":{";
			bool first = true;
			for (const auto& [key, value] : attributes) {
				if (!first) json << ",";
				json << "\"" << escapeJson(key) << "\":\"" << escapeJson(value) << "\"";
				first = false;
			}
			json << "}";
		}

		if (!events.empty()) {
			json << ",\"events\":[";
			for (size_t i = 0; i < events.size(); ++i) {
				if (i > 0) json << ",";
				json << "{\"time\":" << toEpochMicros(events[i].first) << ",";
				json << "\"message\":\"" << escapeJson(events[i].second) << "\"}";
			}
			json << "]";
		}

		json << "}";
		return json.str();
	}

private:
	static std::string escapeJson(const std::string& str) {
		std::string result;
		for (char c : str) {
			switch (c) {
				case '"': result += "\\\""; break;
				case '\\': result += "\\\\"; break;
				case '\n': result += "\\n"; break;
				case '\r': result += "\\r"; break;
				case '\t': result += "\\t"; break;
				default: result += c;
			}
		}
		return result;
	}

	static int64_t toEpochMicros(std::chrono::system_clock::time_point tp) {
		auto duration = tp.time_since_epoch();
		return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
	}
};


/// Trace context for propagating trace IDs across operations.
struct ofxGgmlTraceContext {
	std::string traceId;
	std::string parentSpanId;

	bool isValid() const {
		return !traceId.empty();
	}

	/// Serialize for passing across process boundaries
	std::string serialize() const {
		return traceId + ":" + parentSpanId;
	}

	/// Deserialize from string
	static ofxGgmlTraceContext deserialize(const std::string& str) {
		size_t pos = str.find(':');
		if (pos == std::string::npos) {
			return {str, ""};
		}
		return {str.substr(0, pos), str.substr(pos + 1)};
	}
};


/// RAII span guard that automatically ends span on destruction.
///
/// Example usage:
///     {
///         auto span = tracer.startSpan("model_load");
///         span.setAttribute("model_path", path);
///         loadModel(path); // Work happens here
///         span.setStatus(SpanStatus::Ok);
///     } // Span automatically ends here
///
class ofxGgmlSpanGuard {
public:
	ofxGgmlSpanGuard(std::shared_ptr<ofxGgmlSpan> span, std::function<void(ofxGgmlSpan&)> onEnd)
		: m_span(span), m_onEnd(onEnd) {}

	~ofxGgmlSpanGuard() {
		if (m_span && !m_span->isComplete()) {
			m_span->endTime = std::chrono::system_clock::now();
			if (m_onEnd) {
				m_onEnd(*m_span);
			}
		}
	}

	// Disable copy, enable move
	ofxGgmlSpanGuard(const ofxGgmlSpanGuard&) = delete;
	ofxGgmlSpanGuard& operator=(const ofxGgmlSpanGuard&) = delete;
	ofxGgmlSpanGuard(ofxGgmlSpanGuard&&) = default;
	ofxGgmlSpanGuard& operator=(ofxGgmlSpanGuard&&) = default;

	void setAttribute(const std::string& key, const std::string& value) {
		if (m_span) m_span->attributes[key] = value;
	}

	template<typename T>
	void setAttribute(const std::string& key, const T& value) {
		if (m_span) m_span->attributes[key] = std::to_string(value);
	}

	void addEvent(const std::string& message) {
		if (m_span) {
			m_span->events.push_back({std::chrono::system_clock::now(), message});
		}
	}

	void setStatus(SpanStatus status) {
		if (m_span) m_span->status = status;
	}

	void setError(const std::string& error) {
		if (m_span) {
			m_span->status = SpanStatus::Error;
			m_span->attributes["error"] = error;
		}
	}

	std::string spanId() const {
		return m_span ? m_span->spanId : "";
	}

	std::string traceId() const {
		return m_span ? m_span->traceId : "";
	}

private:
	std::shared_ptr<ofxGgmlSpan> m_span;
	std::function<void(ofxGgmlSpan&)> m_onEnd;
};


/// Distributed tracing system for tracking operations across the inference pipeline.
///
/// This enables:
/// - Performance analysis (where is time spent?)
/// - Request flow visualization
/// - Debugging across async boundaries
/// - Production monitoring
///
/// Example usage:
///     // Start a root span
///     auto span = tracer.startSpan("generate_text");
///     span.setAttribute("model", modelPath);
///     span.setAttribute("prompt_tokens", tokenCount);
///
///     // Child spans automatically link to parent
///     {
///         auto loadSpan = tracer.startSpan("load_model");
///         loadModel();
///         loadSpan.setStatus(SpanStatus::Ok);
///     }
///
///     // Export traces for visualization
///     std::cout << tracer.exportJson() << std::endl;
///
class ofxGgmlTracer {
public:
	ofxGgmlTracer() = default;

	/// Start a new root span (new trace)
	ofxGgmlSpanGuard startSpan(const std::string& name) {
		return startSpanWithContext(name, std::nullopt);
	}

	/// Start a span with explicit trace context (for distributed tracing)
	ofxGgmlSpanGuard startSpanWithContext(
		const std::string& name,
		const std::optional<ofxGgmlTraceContext>& context = std::nullopt
	) {
		auto span = std::make_shared<ofxGgmlSpan>();
		span->name = name;
		span->spanId = generateSpanId();
		span->startTime = std::chrono::system_clock::now();

		if (context && context->isValid()) {
			span->traceId = context->traceId;
			span->parentSpanId = context->parentSpanId;
		} else {
			// Check for active span on current thread
			auto activeContext = getActiveContext();
			if (activeContext) {
				span->traceId = activeContext->traceId;
				span->parentSpanId = activeContext->parentSpanId;
			} else {
				// New root span
				span->traceId = generateTraceId();
			}
		}

		// Set as active span for this thread
		setActiveContext({span->traceId, span->spanId});

		return ofxGgmlSpanGuard(span, [this](ofxGgmlSpan& completedSpan) {
			onSpanEnd(completedSpan);
		});
	}

	/// Get current trace context (for propagating to other threads/processes)
	std::optional<ofxGgmlTraceContext> getCurrentContext() const {
		return getActiveContext();
	}

	/// Export all completed spans as JSON array
	std::string exportJson() const {
		std::lock_guard<std::mutex> lock(m_mutex);

		std::ostringstream json;
		json << "[";
		for (size_t i = 0; i < m_completedSpans.size(); ++i) {
			if (i > 0) json << ",";
			json << m_completedSpans[i].toJson();
		}
		json << "]";
		return json.str();
	}

	/// Export in OpenTelemetry format (simplified)
	std::string exportOpenTelemetry() const {
		std::lock_guard<std::mutex> lock(m_mutex);

		std::ostringstream json;
		json << "{\"resourceSpans\":[{\"scopeSpans\":[{\"spans\":[";
		for (size_t i = 0; i < m_completedSpans.size(); ++i) {
			if (i > 0) json << ",";
			json << m_completedSpans[i].toJson();
		}
		json << "]}]}]}";
		return json.str();
	}

	/// Clear all completed spans
	void clear() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_completedSpans.clear();
	}

	/// Get statistics
	struct Stats {
		size_t totalSpans = 0;
		float avgDurationMs = 0.0f;
		size_t errorCount = 0;
	};

	Stats getStats() const {
		std::lock_guard<std::mutex> lock(m_mutex);

		Stats stats;
		stats.totalSpans = m_completedSpans.size();

		if (stats.totalSpans > 0) {
			float totalDuration = 0.0f;
			for (const auto& span : m_completedSpans) {
				totalDuration += span.durationMs();
				if (span.status == SpanStatus::Error) {
					stats.errorCount++;
				}
			}
			stats.avgDurationMs = totalDuration / stats.totalSpans;
		}

		return stats;
	}

private:
	void onSpanEnd(const ofxGgmlSpan& span) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_completedSpans.push_back(span);

		// Limit memory usage
		if (m_completedSpans.size() > 10000) {
			m_completedSpans.erase(m_completedSpans.begin());
		}
	}

	void setActiveContext(const ofxGgmlTraceContext& context) const {
		thread_local std::optional<ofxGgmlTraceContext> activeContext;
		activeContext = context;
	}

	std::optional<ofxGgmlTraceContext> getActiveContext() const {
		thread_local std::optional<ofxGgmlTraceContext> activeContext;
		return activeContext;
	}

	mutable std::mutex m_mutex;
	std::vector<ofxGgmlSpan> m_completedSpans;
};


/// Global tracer instance (can be replaced with dependency injection)
inline ofxGgmlTracer& globalTracer() {
	static ofxGgmlTracer tracer;
	return tracer;
}


/// Convenience macro for tracing a scope
#define OFXGGML_TRACE_SCOPE(name) \
	auto _trace_span_##__LINE__ = globalTracer().startSpan(name)

#define OFXGGML_TRACE_FUNCTION() \
	OFXGGML_TRACE_SCOPE(__FUNCTION__)
