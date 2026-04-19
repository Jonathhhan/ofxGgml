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
						preset.description = "Recommended model";
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
