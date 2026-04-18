#pragma once

#include "ofMain.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

/// Streaming context for managing backpressure and flow control during inference.
///
/// Provides pause/resume/cancel capabilities and backpressure signals to allow
/// consumers to control generation speed and resource usage.
///
/// Example usage:
/// ```cpp
/// auto ctx = std::make_shared<ofxGgmlStreamingContext>();
///
/// // Set backpressure threshold (pause if more than 1000 chars buffered)
/// ctx->setBackpressureThreshold(1000);
///
/// inference.generate(modelPath, prompt, settings, [ctx](const std::string& chunk) {
///     if (ctx->shouldPause()) {
///         // Consumer is slow, wait for signal
///         ctx->waitForResume(5000); // 5 second timeout
///     }
///     if (ctx->isCancelled()) {
///         return false; // Stop generation
///     }
///     // Process chunk
///     ctx->addConsumedChars(chunk.size());
///     return true;
/// });
/// ```
class ofxGgmlStreamingContext {
public:
	enum class State {
		Active,
		Paused,
		Cancelled,
		Completed
	};

	ofxGgmlStreamingContext()
		: m_state(State::Active)
		, m_bufferedChars(0)
		, m_consumedChars(0)
		, m_backpressureThreshold(0)
		, m_totalChunks(0)
		, m_droppedChunks(0) {
	}

	/// Request the stream to pause. The generator should check shouldPause().
	void pause() {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_state == State::Active) {
			m_state = State::Paused;
		}
	}

	/// Resume a paused stream and notify waiting threads.
	void resume() {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_state == State::Paused) {
				m_state = State::Active;
			}
		}
		m_condition.notify_all();
	}

	/// Cancel the stream. The generator should check isCancelled().
	void cancel() {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_state = State::Cancelled;
		}
		m_condition.notify_all();
	}

	/// Mark the stream as completed.
	void complete() {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_state != State::Cancelled) {
				m_state = State::Completed;
			}
		}
		m_condition.notify_all();
	}

	/// Check if the stream should pause (backpressure signal).
	bool shouldPause() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_backpressureThreshold > 0) {
			return m_bufferedChars >= m_backpressureThreshold;
		}
		return m_state == State::Paused;
	}

	/// Check if the stream has been cancelled.
	bool isCancelled() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_state == State::Cancelled;
	}

	/// Check if the stream is paused.
	bool isPaused() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_state == State::Paused;
	}

	/// Check if the stream is active.
	bool isActive() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_state == State::Active;
	}

	/// Check if the stream is completed.
	bool isCompleted() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_state == State::Completed;
	}

	/// Get current state.
	State getState() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_state;
	}

	/// Wait for the stream to resume. Returns false on timeout or cancellation.
	bool waitForResume(uint64_t timeoutMs = 0) {
		std::unique_lock<std::mutex> lock(m_mutex);
		if (m_state == State::Active || m_state == State::Completed) {
			return true;
		}
		if (m_state == State::Cancelled) {
			return false;
		}
		if (timeoutMs > 0) {
			return m_condition.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
				return m_state == State::Active || m_state == State::Cancelled || m_state == State::Completed;
			});
		} else {
			m_condition.wait(lock, [this]() {
				return m_state == State::Active || m_state == State::Cancelled || m_state == State::Completed;
			});
			return m_state == State::Active || m_state == State::Completed;
		}
	}

	/// Set backpressure threshold in characters. When buffered chars exceed this, shouldPause() returns true.
	void setBackpressureThreshold(size_t threshold) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_backpressureThreshold = threshold;
	}

	/// Get backpressure threshold.
	size_t getBackpressureThreshold() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_backpressureThreshold;
	}

	/// Add to buffered character count (called when chunk is received).
	void addBufferedChars(size_t count) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_bufferedChars += count;
		m_totalChunks++;
	}

	/// Add to consumed character count (called when chunk is processed).
	void addConsumedChars(size_t count) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_consumedChars += count;
		if (m_bufferedChars >= count) {
			m_bufferedChars -= count;
		} else {
			m_bufferedChars = 0;
		}
	}

	/// Reset buffered character count.
	void resetBufferedChars() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_bufferedChars = 0;
	}

	/// Get current buffered character count.
	size_t getBufferedChars() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_bufferedChars;
	}

	/// Get total consumed character count.
	size_t getConsumedChars() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_consumedChars;
	}

	/// Get total chunk count.
	size_t getTotalChunks() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_totalChunks;
	}

	/// Increment dropped chunk count (for monitoring).
	void recordDroppedChunk() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_droppedChunks++;
	}

	/// Get dropped chunk count.
	size_t getDroppedChunks() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_droppedChunks;
	}

	/// Reset statistics.
	void resetStats() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_bufferedChars = 0;
		m_consumedChars = 0;
		m_totalChunks = 0;
		m_droppedChunks = 0;
	}

private:
	mutable std::mutex m_mutex;
	std::condition_variable m_condition;
	State m_state;
	size_t m_bufferedChars;
	size_t m_consumedChars;
	size_t m_backpressureThreshold;
	size_t m_totalChunks;
	size_t m_droppedChunks;
};

using ofxGgmlStreamingContextPtr = std::shared_ptr<ofxGgmlStreamingContext>;
