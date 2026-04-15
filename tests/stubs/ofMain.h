#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
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

using ofJson = nlohmann::json;

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
