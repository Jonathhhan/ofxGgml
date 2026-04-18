#pragma once

#include "ofMain.h"
#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

/// Configurable logger for ofxGgml with multiple log levels and output targets.
///
/// Supports console output, file output, and custom callbacks.
/// Thread-safe for concurrent logging from multiple threads.
///
/// Example usage:
/// ```cpp
/// auto& logger = ofxGgmlLogger::getInstance();
/// logger.setLevel(ofxGgmlLogger::Level::Debug);
/// logger.setFileOutput("ofxGgml.log");
///
/// logger.debug("Model", "Loading model from path: {}", modelPath);
/// logger.info("Inference", "Generated {} tokens in {} ms", count, elapsed);
/// logger.warn("Cache", "Cache miss for key: {}", key);
/// logger.error("Runtime", "Failed to initialize backend: {}", error);
/// ```
class ofxGgmlLogger {
public:
	enum class Level {
		Trace = 0,
		Debug = 1,
		Info = 2,
		Warn = 3,
		Error = 4,
		Critical = 5,
		Off = 6
	};

	using LogCallback = std::function<void(Level, const std::string& component, const std::string& message)>;

	/// Get singleton instance.
	static ofxGgmlLogger& getInstance() {
		static ofxGgmlLogger instance;
		return instance;
	}

	/// Set minimum log level (messages below this level are ignored).
	void setLevel(Level level) {
		m_level.store(static_cast<int>(level), std::memory_order_relaxed);
	}

	/// Get current log level.
	Level getLevel() const {
		return static_cast<Level>(m_level.load(std::memory_order_relaxed));
	}

	/// Enable/disable console output.
	void setConsoleOutput(bool enabled) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_consoleOutput = enabled;
	}

	/// Enable file output (empty path disables file logging).
	void setFileOutput(const std::string& filePath) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_fileStream.is_open()) {
			m_fileStream.close();
		}
		if (!filePath.empty()) {
			m_fileStream.open(filePath, std::ios::out | std::ios::app);
			m_fileOutput = m_fileStream.is_open();
		} else {
			m_fileOutput = false;
		}
	}

	/// Set custom log callback.
	void setCallback(LogCallback callback) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_callback = callback;
	}

	/// Clear custom log callback.
	void clearCallback() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_callback = nullptr;
	}

	/// Enable/disable timestamps in log messages.
	void setTimestampEnabled(bool enabled) {
		m_timestampEnabled.store(enabled, std::memory_order_relaxed);
	}

	/// Log a trace message.
	void trace(const std::string& component, const std::string& message) {
		log(Level::Trace, component, message);
	}

	/// Log a debug message.
	void debug(const std::string& component, const std::string& message) {
		log(Level::Debug, component, message);
	}

	/// Log an info message.
	void info(const std::string& component, const std::string& message) {
		log(Level::Info, component, message);
	}

	/// Log a warning message.
	void warn(const std::string& component, const std::string& message) {
		log(Level::Warn, component, message);
	}

	/// Log an error message.
	void error(const std::string& component, const std::string& message) {
		log(Level::Error, component, message);
	}

	/// Log a critical message.
	void critical(const std::string& component, const std::string& message) {
		log(Level::Critical, component, message);
	}

	/// Flush any buffered output.
	void flush() {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_fileStream.is_open()) {
			m_fileStream.flush();
		}
	}

private:
	ofxGgmlLogger()
		: m_level(static_cast<int>(Level::Info))
		, m_consoleOutput(true)
		, m_fileOutput(false)
		, m_timestampEnabled(true) {
	}

	~ofxGgmlLogger() {
		if (m_fileStream.is_open()) {
			m_fileStream.close();
		}
	}

	ofxGgmlLogger(const ofxGgmlLogger&) = delete;
	ofxGgmlLogger& operator=(const ofxGgmlLogger&) = delete;

	void log(Level level, const std::string& component, const std::string& message) {
		// Fast check without lock
		if (static_cast<int>(level) < m_level.load(std::memory_order_relaxed)) {
			return;
		}

		std::string formatted = formatMessage(level, component, message);

		std::lock_guard<std::mutex> lock(m_mutex);

		if (m_consoleOutput) {
			std::cout << formatted << std::endl;
		}

		if (m_fileOutput && m_fileStream.is_open()) {
			m_fileStream << formatted << std::endl;
		}

		if (m_callback) {
			m_callback(level, component, message);
		}
	}

	std::string formatMessage(Level level, const std::string& component, const std::string& message) {
		std::ostringstream oss;

		if (m_timestampEnabled.load(std::memory_order_relaxed)) {
			oss << "[" << ofGetTimestampString("%Y-%m-%d %H:%M:%S") << "] ";
		}

		oss << "[" << levelToString(level) << "] ";

		if (!component.empty()) {
			oss << "[" << component << "] ";
		}

		oss << message;

		return oss.str();
	}

	static std::string levelToString(Level level) {
		switch (level) {
			case Level::Trace:    return "TRACE";
			case Level::Debug:    return "DEBUG";
			case Level::Info:     return "INFO";
			case Level::Warn:     return "WARN";
			case Level::Error:    return "ERROR";
			case Level::Critical: return "CRITICAL";
			default:              return "UNKNOWN";
		}
	}

	std::mutex m_mutex;
	std::atomic<int> m_level;
	std::atomic<bool> m_timestampEnabled;
	bool m_consoleOutput;
	bool m_fileOutput;
	std::ofstream m_fileStream;
	LogCallback m_callback;
};
