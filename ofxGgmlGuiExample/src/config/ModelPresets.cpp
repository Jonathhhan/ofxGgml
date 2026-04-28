#include "ModelPresets.h"

#include "ofMain.h"
#include "ofJson.h"
#include "ofxGgml.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

// ---------------------------------------------------------------------------
// Model Preset Initialization
// ---------------------------------------------------------------------------

void loadModelPresets(
	std::vector<ModelPreset> & modelPresets,
	std::array<int, kModeCount> & taskDefaultModelIndices,
	const char * catalogPath) {

	modelPresets.clear();
	taskDefaultModelIndices.fill(0);

	auto setFallbackPresets = [&]() {
		modelPresets = {
			{
				"Qwen2.5-1.5B Instruct Q4_K_M",
				"qwen2.5-1.5b-instruct-q4_k_m.gguf",
				"https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf",
				"Alibaba Qwen2.5 — balanced chat model",
				"~1.0 GB", "chat, general"
			},
			{
				"Qwen2.5-Coder-1.5B Instruct Q4_K_M",
				"qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
				"https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
				"Alibaba Qwen2.5-Coder — optimized for code generation",
				"~1.0 GB", "scripting, code generation"
			},
			{
				"Qwen2.5-Coder-7B Instruct Q4_K_M",
				"qwen2.5-coder-7b-instruct-q4_k_m.gguf",
				"https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/main/qwen2.5-coder-7b-instruct-q4_k_m.gguf",
				"Alibaba Qwen2.5-Coder — stronger local repo review and patch planning",
				"~4.7 GB", "repo review, larger code edits, architecture analysis"
			},
			{
				"Jan v1 4B Research Q4_K_M",
				"Jan-v1-4B-Q4_K_M.gguf",
				"https://huggingface.co/janhq/Jan-v1-4B-GGUF/resolve/main/Jan-v1-4B-Q4_K_M.gguf",
				"Jan v1 4B - Qwen3-based research model tuned for search and tool use",
				"~2.5 GB", "web search, citation research, tool-using local assistant"
			}
		};
		// Match the CLI defaults for most modes, but prefer a stronger coder model for Script.
		taskDefaultModelIndices = {0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0};
	};

	// Try to load presets from scripts/model-catalog.json first.
	std::error_code ec;
	std::filesystem::path resolvedCatalogPath;

	if (catalogPath && *catalogPath != '\0') {
		resolvedCatalogPath = catalogPath;
	} else {
		resolvedCatalogPath = ofToDataPath("model-catalog.json", true);
	}

	bool loaded = false;

	if (!std::filesystem::exists(resolvedCatalogPath, ec) || ec) {
		// Fallback: resolve relative to addon root (…/ofxGgmlGuiExample/src -> addon root).
		auto srcPath = std::filesystem::path(__FILE__).parent_path();
		auto addonRoot = std::filesystem::weakly_canonical(srcPath / ".." / ".." / "..", ec);
		if (!ec) {
			resolvedCatalogPath = addonRoot / "scripts" / "model-catalog.json";
		}
	}

	if (std::filesystem::exists(resolvedCatalogPath, ec) && !ec) {
		try {
			std::ifstream in(resolvedCatalogPath);
			ofJson json;
			in >> json;
			if (json.contains("models") && json["models"].is_array()) {
				for (const auto & model : json["models"]) {
					ModelPreset preset;
					preset.name = model.value("name", "");
					preset.filename = model.value("filename", "");
					preset.url = model.value("url", "");
					preset.sizeMB = model.value("size", "");
					preset.bestFor = model.value("best_for", "");
					preset.description = model.value("description", preset.bestFor);
					if (preset.description.empty()) {
						preset.description = "Optional preset";
					}
					if (preset.name.empty() || preset.filename.empty() || preset.url.empty()) {
						continue;
					}
					modelPresets.push_back(std::move(preset));
				}
			}
			if (!modelPresets.empty() &&
				json.contains("task_defaults") && json["task_defaults"].is_object()) {
				auto defaults = json["task_defaults"];
				auto setDefault = [&](const char * key, AiMode mode) {
					const int idxOneBased = defaults.value(key, 0);
					if (idxOneBased > 0) {
						const int idx = std::clamp(idxOneBased - 1, 0,
							static_cast<int>(modelPresets.size()) - 1);
						taskDefaultModelIndices[static_cast<int>(mode)] = idx;
					}
				};
				setDefault("chat", AiMode::Chat);
				setDefault("script", AiMode::Script);
				setDefault("summarize", AiMode::Summarize);
				setDefault("write", AiMode::Write);
				setDefault("translate", AiMode::Translate);
				setDefault("custom", AiMode::Custom);
				setDefault("videoEssay", AiMode::VideoEssay);
				setDefault("longVideo", AiMode::LongVideo);
				setDefault("vision", AiMode::Vision);
				setDefault("speech", AiMode::Speech);
				setDefault("diffusion", AiMode::Diffusion);
				setDefault("milkdrop", AiMode::MilkDrop);
				setDefault("easy", AiMode::Easy);
			}
			loaded = !modelPresets.empty();
		} catch (const std::exception & e) {
			ofLogWarning("ModelPresets") << "Failed to load model-catalog.json: " << e.what();
		} catch (...) {
			ofLogWarning("ModelPresets") << "Failed to load model-catalog.json: unknown parse error";
		}
	}

	if (!loaded) {
		setFallbackPresets();
	}
}

void loadVideoRenderPresets(
	std::vector<VideoRenderPreset> & presets,
	int & recommendedIndex,
	const char * catalogPath) {

	presets.clear();
	recommendedIndex = 0;

	auto setFallbackPresets = [&]() {
		presets = {
			{
				"Wan 2.1 I2V 14B Q4_0",
				"wan2.1-i2v-14b-Q4_0.gguf",
				"https://huggingface.co/models?search=wan%202.1%20i2v%20gguf",
				"Optional preset for planned image-to-video segment rendering.",
				"ofxStableDiffusion",
				"WANI2V",
				"general image-to-video, cinematic motion"
			},
			{
				"Wan 2.1 TI2V 5B Q4_0",
				"wan2.1-ti2v-5b-Q4_0.gguf",
				"https://huggingface.co/models?search=wan%202.1%20ti2v%20gguf",
				"Text-and-image-to-video variant with stronger stylized prompt control.",
				"ofxStableDiffusion",
				"WANTI2V",
				"text-driven video, stylized motion"
			},
			{
				"Wan 2.1 FLF2V 14B Q4_0",
				"wan2.1-flf2v-14b-Q4_0.gguf",
				"https://huggingface.co/models?search=wan%202.1%20flf2v%20gguf",
				"First-last-frame variant for continuity-preserving shot transitions.",
				"ofxStableDiffusion",
				"WANFLF2V",
				"continuity shots, transition control"
			},
			{
				"Wan VACE 14B Q4_0",
				"wan-vace-14b-Q4_0.gguf",
				"https://huggingface.co/models?search=wan%20vace%20gguf",
				"Conditioning-heavy video model for tighter control over motion guidance.",
				"ofxStableDiffusion",
				"WANVACE",
				"conditioned motion, higher control"
			}
		};
		recommendedIndex = 0;
	};

	std::error_code ec;
	std::filesystem::path resolvedCatalogPath;

	if (catalogPath && *catalogPath != '\0') {
		resolvedCatalogPath = catalogPath;
	} else {
		auto srcPath = std::filesystem::path(__FILE__).parent_path();
		auto addonRoot = std::filesystem::weakly_canonical(srcPath / ".." / ".." / "..", ec);
		if (!ec) {
			resolvedCatalogPath = addonRoot / "scripts" / "video-model-catalog.json";
		}
	}

	bool loaded = false;
	if (std::filesystem::exists(resolvedCatalogPath, ec) && !ec) {
		try {
			std::ifstream in(resolvedCatalogPath);
			ofJson json;
			in >> json;
			if (json.contains("models") && json["models"].is_array()) {
				for (const auto & model : json["models"]) {
					VideoRenderPreset preset;
					preset.name = model.value("name", "");
					preset.filename = model.value("filename", "");
					preset.url = model.value("url", "");
					preset.description = model.value("description", "");
					preset.backend = model.value("backend", "ofxStableDiffusion");
					preset.family = model.value("family", "");
					preset.bestFor = model.value("best_for", "");
					if (preset.name.empty() || preset.filename.empty()) {
						continue;
					}
					if (preset.description.empty()) {
						preset.description = "Optional video render preset";
					}
					presets.push_back(std::move(preset));
				}
			}
			if (!presets.empty()) {
				const int idxOneBased = json.value("recommended", 1);
				recommendedIndex = std::clamp(
					idxOneBased - 1,
					0,
					static_cast<int>(presets.size()) - 1);
			}
			loaded = !presets.empty();
		} catch (const std::exception & e) {
			ofLogWarning("ModelPresets") << "Failed to load video-model-catalog.json: " << e.what();
		} catch (...) {
			ofLogWarning("ModelPresets") << "Failed to load video-model-catalog.json: unknown parse error";
		}
	}

	if (!loaded) {
		setFallbackPresets();
	}
}

// ---------------------------------------------------------------------------
// Prompt Template Initialization
// ---------------------------------------------------------------------------

void loadPromptTemplates(std::vector<PromptTemplate> & promptTemplates) {
	promptTemplates.clear();

	// Load default templates from ofxGgml
	for (const auto & preset : ofxGgmlTextAssistant::defaultPromptTemplates()) {
		promptTemplates.push_back({preset.name, preset.systemPrompt});
	}

	// Add additional custom templates
	promptTemplates.push_back({
		"Data Analyst",
		"You are a data analysis expert. Help interpret data, suggest statistical "
		"methods, write queries, and explain results in plain language."
	});
	promptTemplates.push_back({
		"Research Assistant",
		"You are a careful web research assistant. Prefer current source-backed "
		"evidence, separate facts from inference, cite source labels when available, "
		"and say when the provided material is insufficient."
	});
	promptTemplates.push_back({
		"System Architect",
		"You are a software architect. Design systems with clear component "
		"boundaries, data flows, and technology choices. Consider scalability, "
		"reliability, and maintainability."
	});
	promptTemplates.push_back({
		"Debugger",
		"You are an expert debugger. Analyze error messages, stack traces, and "
		"code to identify root causes. Suggest specific fixes and explain why "
		"the bug occurs."
	});
	promptTemplates.push_back({
		"Test Engineer",
		"You are a test engineering expert. Generate comprehensive test cases "
		"including unit tests, edge cases, error paths, and integration scenarios. "
		"Use the appropriate testing framework for the language."
	});
	promptTemplates.push_back({
		"Translator",
		"You are a code translator. Convert code between programming languages "
		"while preserving logic, idioms, and best practices of the target language."
	});
	promptTemplates.push_back({
		"Optimizer",
		"You are a performance optimization expert. Analyze code for bottlenecks, "
		"memory issues, and algorithmic improvements. Suggest concrete optimizations "
		"with expected impact."
	});
}
