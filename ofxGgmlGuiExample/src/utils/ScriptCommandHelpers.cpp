#include "ScriptCommandHelpers.h"
#include "ImGuiHelpers.h"

#include <cctype>
#include <string>

std::string normalizeSlashCommandName(const std::string & raw) {
	std::string normalized;
	normalized.reserve(raw.size());
	for (char ch : raw) {
		if (ch == '-' || ch == '_') {
			continue;
		}
		normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
	}
	return normalized;
}

ScriptSlashCommand parseScriptSlashCommand(const std::string & rawInput) {
	const std::string trimmedInput = trim(rawInput);
	if (trimmedInput.size() < 2 || trimmedInput.front() != '/') {
		return {};
	}

	const size_t splitPos = trimmedInput.find_first_of(" \t\r\n");
	const std::string commandName = normalizeSlashCommandName(
		trimmedInput.substr(1, splitPos == std::string::npos
			? std::string::npos
			: splitPos - 1));
	const std::string argument = splitPos == std::string::npos
		? std::string()
		: trim(trimmedInput.substr(splitPos + 1));

	ScriptSlashCommand command;
	command.argument = argument;
	if (commandName == "help" || commandName == "commands") {
		command.kind = ScriptSlashCommandKind::Help;
	} else if (commandName == "review" || commandName == "reviewall") {
		command.kind = ScriptSlashCommandKind::ReviewAll;
	} else if (commandName == "reviewfix" || commandName == "fixreview") {
		command.kind = ScriptSlashCommandKind::ReviewFix;
	} else if (commandName == "nextedit" || commandName == "next") {
		command.kind = ScriptSlashCommandKind::NextEdit;
	} else if (commandName == "summary" || commandName == "summarize" ||
		commandName == "changes" || commandName == "prsummary") {
		command.kind = ScriptSlashCommandKind::SummarizeChanges;
	} else if (commandName == "tests" || commandName == "testplan") {
		command.kind = ScriptSlashCommandKind::Tests;
	} else if (commandName == "fix" || commandName == "edit") {
		command.kind = ScriptSlashCommandKind::FixPlan;
	} else if (commandName == "explain") {
		command.kind = ScriptSlashCommandKind::Explain;
	} else if (commandName == "docs") {
		command.kind = ScriptSlashCommandKind::Docs;
	}
	return command;
}

std::string buildScriptCommandHelpText() {
	return
		"Slash commands:\n"
		"- /review [focus] -> hierarchical workspace review\n"
		"- /reviewfix [focus] -> review with an actionable fix plan\n"
		"- /nextedit [focus] -> predict the most likely next change\n"
		"- /summary [focus] -> summarize local git changes for reviewers\n"
		"- /tests [focus] -> propose the highest-value tests\n"
		"- /fix [focus] -> produce a structured fix/edit plan\n"
		"- /explain [focus] -> explain code or architecture\n"
		"- /docs [focus] -> answer with grounded docs\n";
}
