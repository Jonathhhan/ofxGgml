#include "ofxGgmlProjectMemory.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace {

std::string trimCopy(const std::string & s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
		++b;
	}
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
		--e;
	}
	return s.substr(b, e - b);
}

std::string stripCodeBlocks(const std::string & text) {
	if (text.find("```") == std::string::npos) return text;

	std::string out;
	out.reserve(text.size());
	size_t pos = 0;
	while (pos < text.size()) {
		size_t fence = text.find("```", pos);
		if (fence == std::string::npos) {
			out.append(text, pos, std::string::npos);
			break;
		}
		out.append(text, pos, fence - pos);
		size_t close = text.find("```", fence + 3);
		out += "\n[code omitted]\n";
		if (close == std::string::npos) break;
		pos = close + 3;
	}
	return out;
}

std::string collapseWhitespace(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	bool inSpace = false;
	for (char c : text) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!inSpace) {
				out.push_back(' ');
				inSpace = true;
			}
		} else {
			out.push_back(c);
			inSpace = false;
		}
	}
	return trimCopy(out);
}

std::string compactForMemory(const std::string & text, size_t maxChars) {
	if (text.empty()) return {};
	std::string compact = collapseWhitespace(stripCodeBlocks(text));
	if (compact.size() <= maxChars) return compact;

	const size_t head = static_cast<size_t>(maxChars * 0.7);
	const size_t tail = (maxChars > head + 24) ? (maxChars - head - 24) : 0;
	if (tail == 0) {
		compact.resize(maxChars);
		return compact;
	}
	return compact.substr(0, head) + " ...[compressed]... " + compact.substr(compact.size() - tail);
}

} // namespace

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

	const size_t reqMax = std::max<size_t>(128, m_entryMaxChars / 3);
	const size_t resMax = std::max<size_t>(256, m_entryMaxChars - reqMax);
	std::string safeRequest = compactForMemory(request, reqMax);
	std::string safeResponse = compactForMemory(response, resMax);

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
		const std::string separator = "\n\n---\n\n";
		std::vector<std::string> entries;
		size_t start = 0;
		while (start <= m_memoryText.size()) {
			size_t sep = m_memoryText.find(separator, start);
			if (sep == std::string::npos) {
				entries.push_back(m_memoryText.substr(start));
				break;
			}
			entries.push_back(m_memoryText.substr(start, sep - start));
			start = sep + separator.size();
		}

		std::string rebuilt;
		for (size_t i = entries.size(); i > 0; --i) {
			const std::string & e = entries[i - 1];
			std::string candidate = rebuilt.empty() ? e : (e + separator + rebuilt);
			if (candidate.size() > m_maxChars) {
				break;
			}
			rebuilt = std::move(candidate);
		}

		if (rebuilt.empty()) {
			rebuilt = m_memoryText.substr(m_memoryText.size() - m_maxChars);
		}
		m_memoryText = std::move(rebuilt);
	}
}

