#pragma once

#include <string>

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

// Normalize slash command name (remove dashes/underscores, lowercase)
std::string normalizeSlashCommandName(const std::string & raw);

// Parse slash command from raw input text
ScriptSlashCommand parseScriptSlashCommand(const std::string & rawInput);

// Build help text for script slash commands
std::string buildScriptCommandHelpText();
