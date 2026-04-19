#pragma once

#include <string>
#include <vector>

enum class ScriptSlashCommandKind {
	None = 0,
	Help,
	ReviewAll,
	ReviewFix,
	NextEdit,
	SummarizeChanges,
	Tests,
	FixPlan,
	Explain,
	Docs
};

struct ScriptSlashCommand {
	ScriptSlashCommandKind kind = ScriptSlashCommandKind::None;
	std::string argument;
};

struct ScriptAtReferenceToken {
	std::string rawToken;
	std::string normalizedToken;
};

// Normalize slash command name (remove dashes/underscores, lowercase)
std::string normalizeSlashCommandName(const std::string & raw);

// Parse slash command from raw input text
ScriptSlashCommand parseScriptSlashCommand(const std::string & rawInput);

// Extract simple IBM-style @references such as @focused or @foo.cpp.
std::vector<ScriptAtReferenceToken> extractScriptAtReferenceTokens(
	const std::string & rawInput);

// Build help text for script slash commands
std::string buildScriptCommandHelpText();
