#include "support/ofxGgmlConversationManager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace {

std::string trimCopy(const std::string & text) {
	const auto begin = std::find_if_not(
		text.begin(),
		text.end(),
		[](unsigned char ch) { return std::isspace(ch) != 0; });
	const auto end = std::find_if_not(
		text.rbegin(),
		text.rend(),
		[](unsigned char ch) { return std::isspace(ch) != 0; }).base();
	if (begin >= end) {
		return {};
	}
	return std::string(begin, end);
}

// Minimal JSON string escaper (no unicode \uXXXX - sufficient for prompt text).
std::string jsonEscape(const std::string & text) {
	std::string out;
	out.reserve(text.size() + 8);
	for (unsigned char ch : text) {
		switch (ch) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (ch < 0x20) {
				// control characters
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(ch));
				out += buf;
			} else {
				out.push_back(static_cast<char>(ch));
			}
			break;
		}
	}
	return out;
}

// Extract unescaped string value from a simple JSON-escaped string literal.
std::string jsonUnescape(const std::string & escaped) {
	std::string out;
	out.reserve(escaped.size());
	for (size_t i = 0; i < escaped.size(); ++i) {
		if (escaped[i] == '\\' && i + 1 < escaped.size()) {
			const char next = escaped[i + 1];
			switch (next) {
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			default: out += next; break;
			}
			++i;
		} else {
			out.push_back(escaped[i]);
		}
	}
	return out;
}

// Tiny parser: extract "key":"value" pairs from a JSON object string.
// Only handles string values (sufficient for our serialization format).
bool extractJsonString(
	const std::string & json,
	const std::string & key,
	size_t & searchPos,
	std::string * valueOut) {
	const std::string needle = "\"" + key + "\":\"";
	const size_t keyPos = json.find(needle, searchPos);
	if (keyPos == std::string::npos) {
		return false;
	}
	const size_t valueStart = keyPos + needle.size();
	std::string value;
	for (size_t i = valueStart; i < json.size(); ++i) {
		if (json[i] == '\\' && i + 1 < json.size()) {
			value.push_back('\\');
			value.push_back(json[i + 1]);
			++i;
		} else if (json[i] == '"') {
			searchPos = i + 1;
			if (valueOut != nullptr) {
				*valueOut = jsonUnescape(value);
			}
			return true;
		} else {
			value.push_back(json[i]);
		}
	}
	return false;
}

ofxGgmlConversationRole roleFromString(const std::string & label) {
	if (label == "system") return ofxGgmlConversationRole::System;
	if (label == "assistant") return ofxGgmlConversationRole::Assistant;
	return ofxGgmlConversationRole::User;
}

const std::string & promptPrefixForRole(
	ofxGgmlConversationRole role,
	const ofxGgmlConversationPromptSettings & settings) {
	switch (role) {
	case ofxGgmlConversationRole::System:
		return settings.systemPrefix;
	case ofxGgmlConversationRole::Assistant:
		return settings.assistantPrefix;
	default:
		return settings.userPrefix;
	}
}

void appendPromptTurn(
	std::ostringstream & out,
	const ofxGgmlConversationTurn & turn,
	const ofxGgmlConversationPromptSettings & settings,
	const std::string & turnSeparator) {
	out << promptPrefixForRole(turn.role, settings)
		<< turn.content
		<< turnSeparator;
}

size_t findFirstUserTurnIndex(
	const std::vector<ofxGgmlConversationTurn> & turns,
	bool preserveFirstUserTurn) {
	if (!preserveFirstUserTurn) {
		return turns.size();
	}

	for (size_t i = 0; i < turns.size(); ++i) {
		if (turns[i].role == ofxGgmlConversationRole::User) {
			return i;
		}
	}
	return turns.size();
}

bool isPreservedTurn(
	const ofxGgmlConversationTurn & turn,
	size_t index,
	size_t firstUserIndex,
	const ofxGgmlConversationPruneSettings & settings) {
	if (settings.preserveSystemTurns &&
		turn.role == ofxGgmlConversationRole::System) {
		return true;
	}
	return index == firstUserIndex;
}

bool extractConversationTurn(
	const std::string & json,
	size_t & searchPos,
	ofxGgmlConversationTurn & turnOut) {
	std::string role;
	std::string content;
	const bool hasRole = extractJsonString(json, "role", searchPos, &role);
	const bool hasContent = extractJsonString(json, "content", searchPos, &content);
	if (!hasRole || !hasContent || role.empty()) {
		return false;
	}
	turnOut = {roleFromString(role), content};
	return true;
}

} // namespace

// ---------------------------------------------------------------------------
// ofxGgmlConversationManager
// ---------------------------------------------------------------------------

ofxGgmlConversationManager::ofxGgmlConversationManager(
	ofxGgmlConversationPruneSettings pruneSettings)
	: m_pruneSettings(std::move(pruneSettings)) {
}

void ofxGgmlConversationManager::addTurn(
	ofxGgmlConversationRole role,
	const std::string & content) {
	m_turns.push_back({role, content});
	if (m_turns.size() > m_pruneSettings.maxTurns) {
		pruneOldTurns();
	}
}

void ofxGgmlConversationManager::addSystemTurn(const std::string & content) {
	addTurn(ofxGgmlConversationRole::System, content);
}

void ofxGgmlConversationManager::addUserTurn(const std::string & content) {
	addTurn(ofxGgmlConversationRole::User, content);
}

void ofxGgmlConversationManager::addAssistantTurn(const std::string & content) {
	addTurn(ofxGgmlConversationRole::Assistant, content);
}

void ofxGgmlConversationManager::clear() {
	m_turns.clear();
}

void ofxGgmlConversationManager::pruneOldTurns() {
	if (m_turns.size() <= m_pruneSettings.targetTurns) {
		return;
	}

	const size_t firstUserIndex = findFirstUserTurnIndex(
		m_turns,
		m_pruneSettings.preserveFirstUserTurn);

	// Build a new turn list, dropping oldest non-preserved turns first.
	size_t toRemove = m_turns.size() - m_pruneSettings.targetTurns;
	std::vector<ofxGgmlConversationTurn> kept;
	kept.reserve(m_turns.size());

	size_t removed = 0;
	for (size_t i = 0; i < m_turns.size(); ++i) {
		const auto & turn = m_turns[i];
		if (removed < toRemove &&
			!isPreservedTurn(
				turn,
				i,
				firstUserIndex,
				m_pruneSettings)) {
			++removed;
		} else {
			kept.push_back(turn);
		}
	}
	m_turns = std::move(kept);
}

size_t ofxGgmlConversationManager::turnCount() const {
	return m_turns.size();
}

bool ofxGgmlConversationManager::isEmpty() const {
	return m_turns.empty();
}

const std::vector<ofxGgmlConversationTurn> &
ofxGgmlConversationManager::getTurns() const {
	return m_turns;
}

std::string ofxGgmlConversationManager::buildPrompt(
	const ofxGgmlConversationPromptSettings & settings) const {
	std::ostringstream out;
	for (const auto & turn : m_turns) {
		appendPromptTurn(out, turn, settings, settings.turnSeparator);
	}
	if (settings.addFinalPromptPrefix) {
		out << settings.assistantPrefix;
	}
	return out.str();
}

std::string ofxGgmlConversationManager::toJson() const {
	std::ostringstream out;
	out << "[";
	for (size_t i = 0; i < m_turns.size(); ++i) {
		const auto & turn = m_turns[i];
		if (i > 0) out << ",";
		out << "{\"role\":\"" << roleLabel(turn.role)
			<< "\",\"content\":\"" << jsonEscape(turn.content) << "\"}";
	}
	out << "]";
	return out.str();
}

bool ofxGgmlConversationManager::fromJson(
	const std::string & json,
	ofxGgmlConversationManager & target) {
	const std::string text = trimCopy(json);
	if (text.empty() || text.front() != '[') {
		return false;
	}

	target.m_turns.clear();
	size_t pos = 0;
	while (pos < text.size()) {
		const size_t objStart = text.find('{', pos);
		if (objStart == std::string::npos) break;
		pos = objStart + 1;

		ofxGgmlConversationTurn turn;
		if (extractConversationTurn(text, pos, turn)) {
			target.m_turns.push_back(std::move(turn));
		}
	}
	return true;
}

ofxGgmlConversationSummaryResult
ofxGgmlConversationManager::summarizeHistory(
	const std::string & modelPath,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlConversationSummaryResult result;
	if (m_turns.empty()) {
		result.error = "Conversation history is empty. Nothing to summarize.";
		return result;
	}

	std::ostringstream prompt;
	prompt
		<< "Summarize the following conversation in 2-4 sentences.\n"
		<< "Focus on the key topics, decisions, and outcomes.\n"
		<< "Do not add new information. Do not use bullet points.\n\n"
		<< "Conversation:\n";

	const ofxGgmlConversationPromptSettings promptSettings;
	for (const auto & turn : m_turns) {
		appendPromptTurn(prompt, turn, promptSettings, "\n");
	}
	prompt << "\nSummary:";

	result.inference = m_inference.generate(
		modelPath,
		prompt.str(),
		settings,
		std::move(onChunk));
	result.success = result.inference.success;
	if (result.success) {
		result.summary = trimCopy(result.inference.text);
	} else {
		result.error = result.inference.error;
	}
	return result;
}

void ofxGgmlConversationManager::setPruneSettings(
	const ofxGgmlConversationPruneSettings & settings) {
	m_pruneSettings = settings;
}

const ofxGgmlConversationPruneSettings &
ofxGgmlConversationManager::getPruneSettings() const {
	return m_pruneSettings;
}

ofxGgmlInference & ofxGgmlConversationManager::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlConversationManager::getInference() const {
	return m_inference;
}

std::string ofxGgmlConversationManager::roleLabel(
	ofxGgmlConversationRole role) {
	switch (role) {
	case ofxGgmlConversationRole::System: return "system";
	case ofxGgmlConversationRole::Assistant: return "assistant";
	default: return "user";
	}
}
