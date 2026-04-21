#pragma once

#include "inference/ofxGgmlInference.h"

#include <functional>
#include <string>
#include <vector>

/// Role of a turn in a conversation.
enum class ofxGgmlConversationRole {
	System = 0,
	User,
	Assistant
};

/// A single turn (role + content) in a conversation.
struct ofxGgmlConversationTurn {
	ofxGgmlConversationRole role = ofxGgmlConversationRole::User;
	std::string content;
};

/// Controls how old turns are pruned to stay within a context budget.
struct ofxGgmlConversationPruneSettings {
	/// Maximum number of turns before pruning is applied.
	size_t maxTurns = 20;
	/// Target turn count after pruning.
	size_t targetTurns = 16;
	/// Always keep system turns even when pruning.
	bool preserveSystemTurns = true;
	/// Keep the very first user turn to preserve conversation anchor.
	bool preserveFirstUserTurn = true;
};

/// Controls how history turns are serialized into a single prompt string.
struct ofxGgmlConversationPromptSettings {
	std::string userPrefix = "User: ";
	std::string assistantPrefix = "Assistant: ";
	std::string systemPrefix = "System: ";
	std::string turnSeparator = "\n";
	/// When true, appends the assistantPrefix at the end to prompt the model.
	bool addFinalPromptPrefix = true;
};

/// Result from the history-summarization helper.
struct ofxGgmlConversationSummaryResult {
	bool success = false;
	std::string error;
	std::string summary;
	ofxGgmlInferenceResult inference;
};

/// Multi-turn conversation history with context-window pruning,
/// prompt assembly, JSON serialization, and LLM-assisted summarization.
class ofxGgmlConversationManager {
public:
	explicit ofxGgmlConversationManager(
		ofxGgmlConversationPruneSettings pruneSettings = {});

	/// Add a turn with an explicit role.
	void addTurn(ofxGgmlConversationRole role, const std::string & content);

	void addSystemTurn(const std::string & content);
	void addUserTurn(const std::string & content);
	void addAssistantTurn(const std::string & content);

	/// Remove all turns.
	void clear();

	/// Prune oldest non-preserved turns until at or below targetTurns.
	void pruneOldTurns();

	size_t turnCount() const;
	bool isEmpty() const;

	const std::vector<ofxGgmlConversationTurn> & getTurns() const;

	/// Build a flat prompt string from the current turn history.
	std::string buildPrompt(
		const ofxGgmlConversationPromptSettings & settings = {}) const;

	/// Serialize the conversation to a compact JSON string.
	std::string toJson() const;

	/// Deserialize a conversation from a JSON string produced by toJson(),
	/// populating the given manager (clears any existing turns first).
	/// Returns true on success, false if the JSON could not be parsed.
	static bool fromJson(
		const std::string & json,
		ofxGgmlConversationManager & target);

	/// Ask the LLM to produce a short summary of the conversation history.
	ofxGgmlConversationSummaryResult summarizeHistory(
		const std::string & modelPath,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	void setPruneSettings(const ofxGgmlConversationPruneSettings & settings);
	const ofxGgmlConversationPruneSettings & getPruneSettings() const;

	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

	/// Human-readable label for a role.
	static std::string roleLabel(ofxGgmlConversationRole role);

private:
	std::vector<ofxGgmlConversationTurn> m_turns;
	ofxGgmlConversationPruneSettings m_pruneSettings;
	ofxGgmlInference m_inference;
};
