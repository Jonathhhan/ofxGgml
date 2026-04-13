#pragma once

#include <cstddef>
#include <string>

/// Lightweight persistent memory helper for coding-assistant style
/// workflows.  Stores request/response pairs and emits a bounded text
/// block that can be prepended to future prompts.
class ofxGgmlProjectMemory {
public:
	/// Enable/disable memory usage when building prompt context.
	void setEnabled(bool enabled);
	bool isEnabled() const;

	/// Max total characters retained in memory text.
	void setMaxChars(size_t maxChars);
	size_t getMaxChars() const;

	/// Remove all stored memory content.
	void clear();
	bool empty() const;

	/// Add a request/response pair. Returns false when either side is
	/// empty and no memory was added.
	bool addInteraction(const std::string & request, const std::string & response);

	/// Direct text access for persistence/UI.
	void setMemoryText(const std::string & text);
	const std::string & getMemoryText() const;

	/// Build prompt context text for inclusion before user input.
	/// Returns empty when disabled or memory is empty.
	std::string buildPromptContext(
		const std::string & heading = "Project memory from previous coding requests:") const;

private:
	void clampMemory();

	bool m_enabled = true;
	size_t m_maxChars = 16000;
	size_t m_entryMaxChars = 3000;
	std::string m_memoryText;
};

