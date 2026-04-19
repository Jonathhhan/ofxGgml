#pragma once

#include "ofFileUtils.h"
#include "ofJson.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
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

inline uint64_t ofGetElapsedTimeMillis() {
	static const auto start = std::chrono::steady_clock::now();
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count());
}

inline std::string ofGetTimestampString(const std::string & format = "%Y-%m-%d-%H-%M-%S-%i") {
	const auto now = std::chrono::system_clock::now();
	const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
	std::tm timeInfo{};
#ifdef _WIN32
	localtime_s(&timeInfo, &nowTime);
#else
	localtime_r(&nowTime, &timeInfo);
#endif
	std::ostringstream oss;
	if (format == "%Y-%m-%d %H:%M:%S") {
		oss << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
	} else {
		oss << std::put_time(&timeInfo, "%Y-%m-%d-%H-%M-%S");
	}
	return oss.str();
}

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
