#include "ModelHelpers.h"
#include "gguf.h"
#include <string>

int detectGgufLayerCountMetadata(const std::string & modelPath) {
	if (modelPath.empty()) {
		return 0;
	}

	struct ggml_context * dataCtx = nullptr;
	struct gguf_init_params params {};
	params.no_alloc = true;
	params.ctx = &dataCtx;

	struct gguf_context * ggufCtx = gguf_init_from_file(modelPath.c_str(), params);
	if (!ggufCtx) {
		return 0;
	}

	auto readString = [&](const std::string & key) -> std::string {
		const int64_t id = gguf_find_key(ggufCtx, key.c_str());
		if (id < 0 || gguf_get_kv_type(ggufCtx, id) != GGUF_TYPE_STRING) {
			return {};
		}
		const char * value = gguf_get_val_str(ggufCtx, id);
		return value ? std::string(value) : std::string();
	};

	auto readInt = [&](const std::string & key) -> int32_t {
		const int64_t id = gguf_find_key(ggufCtx, key.c_str());
		if (id < 0) {
			return 0;
		}
		const enum gguf_type type = gguf_get_kv_type(ggufCtx, id);
		if (type == GGUF_TYPE_INT32) return gguf_get_val_i32(ggufCtx, id);
		if (type == GGUF_TYPE_UINT32) return static_cast<int32_t>(gguf_get_val_u32(ggufCtx, id));
		if (type == GGUF_TYPE_INT16) return static_cast<int32_t>(gguf_get_val_i16(ggufCtx, id));
		if (type == GGUF_TYPE_UINT16) return static_cast<int32_t>(gguf_get_val_u16(ggufCtx, id));
		if (type == GGUF_TYPE_INT8) return static_cast<int32_t>(gguf_get_val_i8(ggufCtx, id));
		if (type == GGUF_TYPE_UINT8) return static_cast<int32_t>(gguf_get_val_u8(ggufCtx, id));
		return 0;
	};

	int detectedLayers = 0;
	const std::string arch = readString("general.architecture");
	if (!arch.empty()) {
		detectedLayers = readInt(arch + ".block_count");
	}

	if (detectedLayers == 0) {
		const char * archNames[] = {
			"llama", "qwen2", "gemma", "phi", "starcoder",
			"gpt2", "mpt", "falcon", "bloom", "mistral"
		};
		for (const auto * name : archNames) {
			detectedLayers = readInt(std::string(name) + ".block_count");
			if (detectedLayers > 0) {
				break;
			}
		}
	}

	gguf_free(ggufCtx);
	if (dataCtx) {
		ggml_free(dataCtx);
	}
	return detectedLayers;
}
