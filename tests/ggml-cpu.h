#pragma once

#include "ggml-backend.h"

inline ggml_backend_t ggml_backend_cpu_init() {
	auto * backend = new ggml_backend();
	++ggml_test_backend_live_count();
	return backend;
}
