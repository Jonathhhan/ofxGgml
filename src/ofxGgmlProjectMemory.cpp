#include "ofxGgmlProjectMemory.h"

void ofxGgmlProjectMemory::setEnabled(bool enabled) {
	m_enabled = enabled;
}

bool ofxGgmlProjectMemory::isEnabled() const {
	return m_enabled;
}

void ofxGgmlProjectMemory::setMaxChars(size_t maxChars) {
	m_maxChars = maxChars;
	clampMemory();
}

size_t ofxGgmlProjectMemory::getMaxChars() const {
	return m_maxChars;
}

void ofxGgmlProjectMemory::clear() {
	m_memoryText.clear();
}

bool ofxGgmlProjectMemory::empty() const {
	return m_memoryText.empty();
}

bool ofxGgmlProjectMemory::addInteraction(const std::string & request, const std::string & response) {
	if (request.empty() || response.empty()) return false;

	std::string safeRequest = request;
	if (safeRequest.size() > m_entryMaxChars) {
		safeRequest.resize(m_entryMaxChars);
	}
	std::string safeResponse = response;
	if (safeResponse.size() > m_entryMaxChars) {
		safeResponse.resize(m_entryMaxChars);
	}

	// Pre-allocate before any appends to avoid reallocation
	const size_t separator_size = m_memoryText.empty() ? 0 : 8; // "\n\n---\n\n"
	const size_t total_needed = m_memoryText.size() + separator_size +
	                             9 + safeRequest.size() +  // "Request:\n"
	                             11 + safeResponse.size(); // "\n\nResponse:\n"
	m_memoryText.reserve(total_needed);

	if (!m_memoryText.empty()) {
		m_memoryText += "\n\n---\n\n";
	}
	m_memoryText += "Request:\n";
	m_memoryText += safeRequest;
	m_memoryText += "\n\nResponse:\n";
	m_memoryText += safeResponse;
	clampMemory();
	return true;
}

void ofxGgmlProjectMemory::setMemoryText(const std::string & text) {
	m_memoryText = text;
	clampMemory();
}

const std::string & ofxGgmlProjectMemory::getMemoryText() const {
	return m_memoryText;
}

std::string ofxGgmlProjectMemory::buildPromptContext(const std::string & heading) const {
	if (!m_enabled || m_memoryText.empty()) return {};
	std::string result;
	result.reserve(heading.size() + m_memoryText.size() + 3);
	result = heading;
	result += '\n';
	result += m_memoryText;
	result += "\n\n";
	return result;
}

void ofxGgmlProjectMemory::clampMemory() {
	if (m_maxChars == 0) {
		m_memoryText.clear();
		return;
	}
	if (m_memoryText.size() > m_maxChars) {
		// Use erase() for in-place modification instead of substr which creates a copy
		m_memoryText.erase(0, m_memoryText.size() - m_maxChars);
	}
}

