#pragma once

#include <string>

// ---------------------------------------------------------------------------
// Text Prompt Utilities
// ---------------------------------------------------------------------------

// Build a structured prompt with system prompt, instruction, input, and output heading
std::string buildStructuredTextPrompt(
	const std::string & systemPrompt,
	const std::string & instruction,
	const std::string & inputHeading,
	const std::string & inputText,
	const std::string & outputHeading);

// Clean up chat output by removing trailing whitespace and artifacts
std::string cleanChatOutput(const std::string & text);

// Clamp prompt to fit within context token limits
std::string clampPromptToContext(
	const std::string & prompt,
	size_t contextTokens,
	bool & trimmed);

// Truncate prompt payload for display purposes
std::string truncatePromptPayload(const std::string & text, size_t maxChars);

// Check if output appears to be cut off mid-sentence
bool isLikelyCutoffOutput(const std::string & text, int aiMode);
