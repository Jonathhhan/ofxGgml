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
		safeRequest = safeRequest.substr(0, m_entryMaxChars);
	}
	std::string safeResponse = response;
	if (safeResponse.size() > m_entryMaxChars) {
		safeResponse = safeResponse.substr(0, m_entryMaxChars);
	}

	if (!m_memoryText.empty()) {
		m_memoryText += "\n\n---\n\n";
	}
	m_memoryText += "Request:\n" + safeRequest + "\n\nResponse:\n" + safeResponse;
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
	return heading + "\n" + m_memoryText + "\n\n";
}

void ofxGgmlProjectMemory::clampMemory() {
	if (m_maxChars == 0) {
		m_memoryText.clear();
		return;
	}
	if (m_memoryText.size() > m_maxChars) {
		m_memoryText = m_memoryText.substr(m_memoryText.size() - m_maxChars);
	}
}

