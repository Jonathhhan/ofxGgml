#pragma once

/// RAII wrappers for ggml resource management.
///
/// These guard classes ensure proper cleanup of ggml resources even when
/// exceptions or early returns occur, preventing resource leaks and
/// use-after-free bugs.

#include "ggml-backend.h"

#include <utility>

/// RAII wrapper for ggml_backend_t
class GgmlBackendGuard {
public:
	GgmlBackendGuard() = default;

	explicit GgmlBackendGuard(ggml_backend_t backend)
		: m_backend(backend) {}

	~GgmlBackendGuard() {
		reset();
	}

	// Non-copyable
	GgmlBackendGuard(const GgmlBackendGuard &) = delete;
	GgmlBackendGuard & operator=(const GgmlBackendGuard &) = delete;

	// Movable
	GgmlBackendGuard(GgmlBackendGuard && other) noexcept
		: m_backend(other.m_backend) {
		other.m_backend = nullptr;
	}

	GgmlBackendGuard & operator=(GgmlBackendGuard && other) noexcept {
		if (this != &other) {
			reset();
			m_backend = other.m_backend;
			other.m_backend = nullptr;
		}
		return *this;
	}

	/// Release the backend resource and free it
	void reset(ggml_backend_t backend = nullptr) {
		if (m_backend) {
			ggml_backend_free(m_backend);
		}
		m_backend = backend;
	}

	/// Release ownership without freeing
	[[nodiscard]] ggml_backend_t release() noexcept {
		ggml_backend_t tmp = m_backend;
		m_backend = nullptr;
		return tmp;
	}

	/// Get the raw pointer
	[[nodiscard]] ggml_backend_t get() const noexcept {
		return m_backend;
	}

	/// Check if valid
	[[nodiscard]] explicit operator bool() const noexcept {
		return m_backend != nullptr;
	}

private:
	ggml_backend_t m_backend = nullptr;
};

/// RAII wrapper for ggml_backend_buffer_t
class GgmlBackendBufferGuard {
public:
	GgmlBackendBufferGuard() = default;

	explicit GgmlBackendBufferGuard(ggml_backend_buffer_t buffer)
		: m_buffer(buffer) {}

	~GgmlBackendBufferGuard() {
		reset();
	}

	// Non-copyable
	GgmlBackendBufferGuard(const GgmlBackendBufferGuard &) = delete;
	GgmlBackendBufferGuard & operator=(const GgmlBackendBufferGuard &) = delete;

	// Movable
	GgmlBackendBufferGuard(GgmlBackendBufferGuard && other) noexcept
		: m_buffer(other.m_buffer) {
		other.m_buffer = nullptr;
	}

	GgmlBackendBufferGuard & operator=(GgmlBackendBufferGuard && other) noexcept {
		if (this != &other) {
			reset();
			m_buffer = other.m_buffer;
			other.m_buffer = nullptr;
		}
		return *this;
	}

	/// Release the buffer resource and free it
	void reset(ggml_backend_buffer_t buffer = nullptr) {
		if (m_buffer) {
			ggml_backend_buffer_free(m_buffer);
		}
		m_buffer = buffer;
	}

	/// Release ownership without freeing
	[[nodiscard]] ggml_backend_buffer_t release() noexcept {
		ggml_backend_buffer_t tmp = m_buffer;
		m_buffer = nullptr;
		return tmp;
	}

	/// Get the raw pointer
	[[nodiscard]] ggml_backend_buffer_t get() const noexcept {
		return m_buffer;
	}

	/// Check if valid
	[[nodiscard]] explicit operator bool() const noexcept {
		return m_buffer != nullptr;
	}

private:
	ggml_backend_buffer_t m_buffer = nullptr;
};

/// RAII wrapper for ggml_backend_sched_t
class GgmlBackendSchedGuard {
public:
	GgmlBackendSchedGuard() = default;

	explicit GgmlBackendSchedGuard(ggml_backend_sched_t sched)
		: m_sched(sched) {}

	~GgmlBackendSchedGuard() {
		reset();
	}

	// Non-copyable
	GgmlBackendSchedGuard(const GgmlBackendSchedGuard &) = delete;
	GgmlBackendSchedGuard & operator=(const GgmlBackendSchedGuard &) = delete;

	// Movable
	GgmlBackendSchedGuard(GgmlBackendSchedGuard && other) noexcept
		: m_sched(other.m_sched) {
		other.m_sched = nullptr;
	}

	GgmlBackendSchedGuard & operator=(GgmlBackendSchedGuard && other) noexcept {
		if (this != &other) {
			reset();
			m_sched = other.m_sched;
			other.m_sched = nullptr;
		}
		return *this;
	}

	/// Release the scheduler resource and free it
	void reset(ggml_backend_sched_t sched = nullptr) {
		if (m_sched) {
			ggml_backend_sched_free(m_sched);
		}
		m_sched = sched;
	}

	/// Release ownership without freeing
	[[nodiscard]] ggml_backend_sched_t release() noexcept {
		ggml_backend_sched_t tmp = m_sched;
		m_sched = nullptr;
		return tmp;
	}

	/// Get the raw pointer
	[[nodiscard]] ggml_backend_sched_t get() const noexcept {
		return m_sched;
	}

	/// Check if valid
	[[nodiscard]] explicit operator bool() const noexcept {
		return m_sched != nullptr;
	}

private:
	ggml_backend_sched_t m_sched = nullptr;
};
