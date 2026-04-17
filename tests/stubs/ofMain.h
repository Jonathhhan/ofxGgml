#pragma once

#include "ofFileUtils.h"

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Minimal openFrameworks stub used for headless unit tests.
// Full addon functionality that depends on OF runtime pieces is tested in
// OFXGGML_WITH_OPENFRAMEWORKS mode.

enum ofLogLevel {
    OF_LOG_VERBOSE = 0,
    OF_LOG_NOTICE,
    OF_LOG_WARNING,
    OF_LOG_ERROR,
    OF_LOG_FATAL_ERROR,
    OF_LOG_SILENT
};

inline void ofLog(ofLogLevel, const std::string &) {}

struct ofLogStubStream {
	template<typename T>
	ofLogStubStream & operator<<(const T &) { return *this; }
};

inline ofLogStubStream ofLogWarning(const std::string &) { return {}; }

template <typename T>
inline std::string ofToString(const T & v) {
	std::ostringstream oss;
	oss << v;
	return oss.str();
}

class ofJson {
public:
	enum class Type {
		Discarded,
		Null,
		Number,
		String,
		Array,
		Object
	};

	ofJson()
		: m_type(Type::Null) {}

	static ofJson parse(const std::string & text, void * = nullptr, bool = true) {
		const std::string trimmed = trim(text);
		if (trimmed.empty()) return ofJson();

		if (trimmed.front() == '[' && trimmed.back() == ']') {
			ofJson arr;
			arr.m_type = Type::Array;
			const std::string body = trim(trimmed.substr(1, trimmed.size() - 2));
			if (body.empty()) return arr;

			size_t start = 0;
			while (start < body.size()) {
				size_t comma = body.find(',', start);
				if (comma == std::string::npos) comma = body.size();
				const std::string token = trim(body.substr(start, comma - start));
				char * end = nullptr;
				const double value = std::strtod(token.c_str(), &end);
				if (end == token.c_str() || (end && *end != '\0')) {
					ofJson discarded;
					discarded.m_type = Type::Discarded;
					return discarded;
				}
				ofJson num;
				num.m_type = Type::Number;
				num.m_number = value;
				arr.m_array.push_back(num);
				start = comma + 1;
			}
			return arr;
		}

		if (trimmed.front() == '{' && trimmed.back() == '}') {
			ofJson obj;
			obj.m_type = Type::Object;
			return obj;
		}

		char * end = nullptr;
		const double scalar = std::strtod(trimmed.c_str(), &end);
		if (end != trimmed.c_str() && end && *end == '\0') {
			ofJson num;
			num.m_type = Type::Number;
			num.m_number = scalar;
			return num;
		}

		ofJson discarded;
		discarded.m_type = Type::Discarded;
		return discarded;
	}

	bool is_discarded() const { return m_type == Type::Discarded; }
	bool is_array() const { return m_type == Type::Array; }
	bool is_object() const { return m_type == Type::Object; }
	bool is_number() const { return m_type == Type::Number; }
	bool is_string() const { return m_type == Type::String; }

	template<typename T>
	T get() const {
		if constexpr (std::is_same_v<T, std::string>) {
			return m_string;
		} else {
			return static_cast<T>(m_number);
		}
	}

	bool contains(const std::string & key) const {
		return m_type == Type::Object && m_object.find(key) != m_object.end();
	}

	const ofJson & operator[](const std::string & key) const {
		auto it = m_object.find(key);
		return it != m_object.end() ? it->second : nullValue();
	}

	const ofJson & operator[](size_t index) const {
		if (index < m_array.size()) return m_array[index];
		return nullValue();
	}

	template<typename T>
	T value(const std::string & key, const T & fallback) const {
		const auto it = m_object.find(key);
		if (it == m_object.end()) return fallback;
		if constexpr (std::is_same_v<T, std::string>) {
			if (it->second.m_type == Type::String) return it->second.m_string;
			return fallback;
		}
		if constexpr (std::is_arithmetic_v<T>) {
			if (it->second.m_type == Type::Number) return static_cast<T>(it->second.m_number);
			return fallback;
		}
		return fallback;
	}

	std::string value(const std::string & key, const char * fallback) const {
		const auto it = m_object.find(key);
		if (it == m_object.end()) return std::string(fallback ? fallback : "");
		if (it->second.m_type == Type::String) return it->second.m_string;
		return std::string(fallback ? fallback : "");
	}

	std::vector<ofJson>::const_iterator begin() const { return m_array.begin(); }
	std::vector<ofJson>::const_iterator end() const { return m_array.end(); }

private:
	static const ofJson & nullValue() {
		static const ofJson v;
		return v;
	}

	static std::string trim(const std::string & s) {
		size_t b = 0;
		while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
		size_t e = s.size();
		while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
		return s.substr(b, e - b);
	}

	Type m_type = Type::Null;
	double m_number = 0.0;
	std::string m_string;
	std::vector<ofJson> m_array;
	std::unordered_map<std::string, ofJson> m_object;
};

struct ofBuffer {
	std::string text;
	std::string getText() const { return text; }
};

struct ofHttpResponse {
	int status = 599;
	ofBuffer data;
};

inline ofHttpResponse ofLoadURL(const std::string &) {
	return {};
}

class ofThread {
public:
	ofThread() = default;
	virtual ~ofThread() {
		stopThread();
		waitForThread(true);
	}

	void startThread() {
		if (m_running.load(std::memory_order_acquire)) return;
		m_stopRequested.store(false, std::memory_order_release);
		m_running.store(true, std::memory_order_release);
		m_worker = std::thread([this]() {
			threadedFunction();
			m_running.store(false, std::memory_order_release);
		});
	}

	void stopThread() {
		m_stopRequested.store(true, std::memory_order_release);
		m_running.store(false, std::memory_order_release);
	}

	void waitForThread(bool) {
		if (m_worker.joinable()) m_worker.join();
	}

	bool isThreadRunning() const {
		return m_running.load(std::memory_order_acquire) &&
			!m_stopRequested.load(std::memory_order_acquire);
	}

protected:
	virtual void threadedFunction() {}

private:
	std::atomic<bool> m_running{false};
	std::atomic<bool> m_stopRequested{false};
	std::thread m_worker;
};

template<typename T>
class ofEvent {};

template<typename TSender, typename T>
inline void ofNotifyEvent(ofEvent<T> &, T &, TSender *) {}

template<typename T>
class ofThreadChannel {
public:
	void send(const T & value) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.push(value);
	}

	void send(T && value) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.push(std::move(value));
	}

	bool tryReceive(T & out) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.empty()) return false;
		out = std::move(m_queue.front());
		m_queue.pop();
		return true;
	}

private:
	std::mutex m_mutex;
	std::queue<T> m_queue;
};
