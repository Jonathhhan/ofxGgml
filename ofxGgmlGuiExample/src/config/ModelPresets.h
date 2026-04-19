#pragma once

#include <array>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Mode — the active AI task tab.
// ---------------------------------------------------------------------------

enum class AiMode {
	Chat,
	Script,
	Summarize,
	Write,
	Translate,
	Custom,
	VideoEssay,
	Vision,
	Speech,
	Tts,
	Diffusion,
	Clip,
	MilkDrop
};

// ---------------------------------------------------------------------------
// ModelPreset — a recommended model with download metadata.
// ---------------------------------------------------------------------------

struct ModelPreset {
	std::string name;         // e.g. "TinyLlama 1.1B Chat"
	std::string filename;     // e.g. "tinyllama-1.1b-chat-v1.0.Q4_0.gguf"
	std::string url;
	std::string description;
	std::string sizeMB;       // human-readable, e.g. "~600 MB"
	std::string bestFor;      // e.g. "chat, general"
};

// ---------------------------------------------------------------------------
// PromptTemplate — predefined system prompt for the Custom panel.
// ---------------------------------------------------------------------------

struct PromptTemplate {
	std::string name;           // e.g. "Code Reviewer"
	std::string systemPrompt;   // the system prompt text
};

// ---------------------------------------------------------------------------
// Model Preset Initialization
// ---------------------------------------------------------------------------

constexpr int kModeCount = 13;

void loadModelPresets(
	std::vector<ModelPreset> & modelPresets,
	std::array<int, kModeCount> & taskDefaultModelIndices,
	const char * catalogPath = nullptr);

void loadPromptTemplates(std::vector<PromptTemplate> & promptTemplates);
