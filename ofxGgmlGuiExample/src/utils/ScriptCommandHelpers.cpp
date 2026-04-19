#include "ScriptCommandHelpers.h"
#include "ImGuiHelpers.h"

#include <cctype>
#include <string>
#include <vector>

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

std::vector<ScriptAtReferenceToken> extractScriptAtReferenceTokens(
	const std::string & rawInput) {
	std::vector<ScriptAtReferenceToken> tokens;
	const std::string trimmedInput = trim(rawInput);
	if (trimmedInput.empty()) {
		return tokens;
	}

	auto normalizeToken = [](const std::string & raw) {
		std::string normalized;
		normalized.reserve(raw.size());
		for (char ch : raw) {
			if (ch == '\\') {
				normalized.push_back('/');
			} else {
				normalized.push_back(
					static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			}
		}
		return normalized;
	};

	const auto isReferenceBoundary = [](char ch) {
		return std::isspace(static_cast<unsigned char>(ch)) != 0 ||
			ch == ',' || ch == ';' || ch == ')' || ch == '(' ||
			ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
			ch == '"' || ch == '\'';
	};

	for (size_t i = 0; i < trimmedInput.size(); ++i) {
		if (trimmedInput[i] != '@') {
			continue;
		}
		if (i > 0) {
			const char prev = trimmedInput[i - 1];
			if (!std::isspace(static_cast<unsigned char>(prev)) &&
				prev != '(' && prev != '[' && prev != '{' && prev != '"') {
				continue;
			}
		}

		size_t end = i + 1;
		while (end < trimmedInput.size() && !isReferenceBoundary(trimmedInput[end])) {
			++end;
		}
		if (end <= i + 1) {
			continue;
		}

		ScriptAtReferenceToken token;
		token.rawToken = trimmedInput.substr(i + 1, end - i - 1);
		token.normalizedToken = normalizeToken(token.rawToken);
		tokens.push_back(token);
		i = end - 1;
	}

	return tokens;
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
		"- /docs [focus] -> answer with grounded docs\n"
		"\n"
		"@ references:\n"
		"- @focused -> use the selected file\n"
		"- @recent -> use recent touched files\n"
		"- @workspace -> favor whole-workspace context\n"
		"- @filename.cpp -> resolve a loaded file by name\n";
}
