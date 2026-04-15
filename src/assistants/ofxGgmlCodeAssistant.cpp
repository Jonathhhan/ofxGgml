#include "ofxGgmlCodeAssistant.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

std::string trimCopy(const std::string & s) {
	size_t start = 0;
	while (start < s.size() &&
		std::isspace(static_cast<unsigned char>(s[start]))) {
		++start;
	}
	size_t end = s.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		--end;
	}
	return s.substr(start, end - start);
}

std::string toLowerCopy(const std::string & s) {
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return out;
}

std::string truncateWithMarker(const std::string & text, size_t maxChars) {
	if (maxChars == 0 || text.size() <= maxChars) {
		return text;
	}
	if (maxChars <= 32) {
		return text.substr(0, maxChars);
	}
	return text.substr(0, maxChars) + "\n...[truncated]";
}

std::vector<std::string> splitLines(const std::string & text) {
	std::vector<std::string> lines;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		lines.push_back(line);
	}
	return lines;
}

std::vector<std::string> splitPipeFields(const std::string & text) {
	std::vector<std::string> fields;
	std::string current;
	std::istringstream stream(text);
	while (std::getline(stream, current, '|')) {
		fields.push_back(trimCopy(current));
	}
	return fields;
}

std::vector<std::string> tokenizeQuery(const std::string & text) {
	std::vector<std::string> tokens;
	std::unordered_set<std::string> seen;
	std::string current;
	for (char c : text) {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
			current.push_back(static_cast<char>(std::tolower(
				static_cast<unsigned char>(c))));
		} else if (!current.empty()) {
			if (current.size() >= 2 && seen.insert(current).second) {
				tokens.push_back(current);
			}
			current.clear();
		}
	}
	if (!current.empty() && current.size() >= 2 && seen.insert(current).second) {
		tokens.push_back(current);
	}
	return tokens;
}

std::vector<std::string> tokenizeIdentifier(const std::string & text) {
	std::vector<std::string> tokens;
	std::unordered_set<std::string> seen;
	std::string current;

	auto flushCurrent = [&]() {
		if (current.size() >= 2) {
			std::string lowered = toLowerCopy(current);
			if (seen.insert(lowered).second) {
				tokens.push_back(std::move(lowered));
			}
		}
		current.clear();
	};

	for (size_t i = 0; i < text.size(); ++i) {
		const unsigned char uc = static_cast<unsigned char>(text[i]);
		if (!(std::isalnum(uc) || text[i] == '_')) {
			flushCurrent();
			continue;
		}
		if (!current.empty()) {
			const unsigned char prev =
				static_cast<unsigned char>(current.back());
			const bool camelBoundary =
				std::islower(prev) && std::isupper(uc);
			const bool digitBoundary =
				(std::isalpha(prev) && std::isdigit(uc)) ||
				(std::isdigit(prev) && std::isalpha(uc));
			if (camelBoundary || digitBoundary || text[i] == '_') {
				flushCurrent();
				if (text[i] == '_') {
					continue;
				}
			}
		}
		if (text[i] != '_') {
			current.push_back(static_cast<char>(uc));
		}
	}
	flushCurrent();
	return tokens;
}

bool containsToken(
	const std::vector<std::string> & haystackTokens,
	const std::string & token) {
	return std::find(haystackTokens.begin(), haystackTokens.end(), token) !=
		haystackTokens.end();
}

std::string joinStrings(
	const std::vector<std::string> & values,
	const std::string & separator) {
	std::ostringstream stream;
	for (size_t i = 0; i < values.size(); ++i) {
		if (i > 0) {
			stream << separator;
		}
		stream << values[i];
	}
	return stream.str();
}

std::string unescapeTaggedValue(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\' && i + 1 < text.size()) {
			const char next = text[i + 1];
			if (next == 'n') {
				out.push_back('\n');
				++i;
				continue;
			}
			if (next == 't') {
				out.push_back('\t');
				++i;
				continue;
			}
			if (next == '\\') {
				out.push_back('\\');
				++i;
				continue;
			}
		}
		out.push_back(text[i]);
	}
	return out;
}

std::string escapeTaggedValue(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	for (char c : text) {
		switch (c) {
		case '\n':
			out += "\\n";
			break;
		case '\t':
			out += "\\t";
			break;
		case '\\':
			out += "\\\\";
			break;
		default:
			out.push_back(c);
			break;
		}
	}
	return out;
}

std::string patchKindToString(ofxGgmlCodeAssistantPatchKind kind) {
	switch (kind) {
	case ofxGgmlCodeAssistantPatchKind::WriteFile:
		return "write";
	case ofxGgmlCodeAssistantPatchKind::ReplaceTextOp:
		return "replace";
	case ofxGgmlCodeAssistantPatchKind::AppendText:
		return "append";
	case ofxGgmlCodeAssistantPatchKind::DeleteFileOp:
		return "delete";
	}
	return "write";
}

ofxGgmlCodeAssistantPatchKind parsePatchKind(const std::string & text) {
	const std::string lowered = toLowerCopy(trimCopy(text));
	if (lowered == "replace") {
		return ofxGgmlCodeAssistantPatchKind::ReplaceTextOp;
	}
	if (lowered == "append") {
		return ofxGgmlCodeAssistantPatchKind::AppendText;
	}
	if (lowered == "delete") {
		return ofxGgmlCodeAssistantPatchKind::DeleteFileOp;
	}
	return ofxGgmlCodeAssistantPatchKind::WriteFile;
}

bool isLikelyCallerLine(
	const std::string & trimmedLine,
	const std::string & symbolName) {
	if (trimmedLine.empty() || symbolName.empty()) {
		return false;
	}

	const std::string lowered = toLowerCopy(trimmedLine);
	const std::string loweredName = toLowerCopy(symbolName);
	const size_t pos = lowered.find(loweredName);
	if (pos == std::string::npos) {
		return false;
	}

	const bool hasOpenParen =
		lowered.find('(', pos + loweredName.size()) != std::string::npos;
	if (!hasOpenParen) {
		return false;
	}

	if (trimmedLine.rfind("class ", 0) == 0 ||
		trimmedLine.rfind("struct ", 0) == 0 ||
		trimmedLine.rfind("enum ", 0) == 0 ||
		trimmedLine.rfind("namespace ", 0) == 0 ||
		trimmedLine.rfind("def ", 0) == 0 ||
		trimmedLine.rfind("function ", 0) == 0) {
		return false;
	}

	if (trimmedLine.find("::" + symbolName + "(") != std::string::npos ||
		trimmedLine.find(symbolName + "(") != std::string::npos) {
		const bool startsWithDefinition =
			trimmedLine.find(" " + symbolName + "(") != std::string::npos &&
			trimmedLine.find('{') != std::string::npos;
		if (startsWithDefinition) {
			return false;
		}
	}

	return true;
}

std::string normalizeRelativeFilePath(const std::string & path) {
	if (path.empty()) {
		return {};
	}
	return std::filesystem::path(path).generic_string();
}

std::vector<std::string> splitEscapedLines(const std::string & text) {
	return splitLines(unescapeTaggedValue(text));
}

struct CompileCommandsIndex {
	std::string path;
	std::unordered_set<std::string> files;
};

bool hasExtension(
	const std::string & path,
	const std::initializer_list<const char *> & extensions) {
	const std::string ext = toLowerCopy(std::filesystem::path(path).extension().string());
	for (const auto * candidate : extensions) {
		if (ext == candidate) {
			return true;
		}
	}
	return false;
}

bool isCppLikeFile(const std::string & path) {
	return hasExtension(path, {
		".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"
	});
}

bool isPythonLikeFile(const std::string & path) {
	return hasExtension(path, {".py"});
}

std::string joinScope(const std::vector<std::string> & scopes) {
	if (scopes.empty()) {
		return {};
	}
	std::ostringstream stream;
	for (size_t i = 0; i < scopes.size(); ++i) {
		if (i > 0) {
			stream << "::";
		}
		stream << scopes[i];
	}
	return stream.str();
}

ofxGgmlCodeAssistantSourceRange estimateBraceRange(
	const std::vector<std::string> & lines,
	size_t startIndex) {
	ofxGgmlCodeAssistantSourceRange range;
	range.startLine = static_cast<int>(startIndex + 1);
	range.startColumn = 1;
	range.endLine = range.startLine;
	range.endColumn = static_cast<int>(lines[startIndex].size()) + 1;

	int depth = 0;
	bool seenOpenBrace = false;
	for (size_t i = startIndex; i < lines.size(); ++i) {
		for (char ch : lines[i]) {
			if (ch == '{') {
				++depth;
				seenOpenBrace = true;
			} else if (ch == '}') {
				--depth;
			}
			if (seenOpenBrace && depth == 0) {
				range.endLine = static_cast<int>(i + 1);
				range.endColumn = static_cast<int>(lines[i].size()) + 1;
				return range;
			}
		}
		if (!seenOpenBrace && lines[i].find(';') != std::string::npos) {
			range.endLine = static_cast<int>(i + 1);
			range.endColumn = static_cast<int>(lines[i].size()) + 1;
			return range;
		}
	}
	return range;
}

ofxGgmlCodeAssistantSourceRange estimateIndentRange(
	const std::vector<std::string> & lines,
	size_t startIndex) {
	ofxGgmlCodeAssistantSourceRange range;
	range.startLine = static_cast<int>(startIndex + 1);
	range.startColumn = 1;
	range.endLine = range.startLine;
	range.endColumn = static_cast<int>(lines[startIndex].size()) + 1;

	const std::string firstLine = lines[startIndex];
	const size_t firstIndent = firstLine.find_first_not_of(" \t");
	if (firstIndent == std::string::npos) {
		return range;
	}

	for (size_t i = startIndex + 1; i < lines.size(); ++i) {
		const std::string trimmed = trimCopy(lines[i]);
		if (trimmed.empty()) {
			continue;
		}
		const size_t indent = lines[i].find_first_not_of(" \t");
		if (indent == std::string::npos || indent <= firstIndent) {
			range.endLine = static_cast<int>(i);
			range.endColumn = static_cast<int>(lines[i - 1].size()) + 1;
			return range;
		}
	}

	range.endLine = static_cast<int>(lines.size());
	range.endColumn = lines.empty()
		? 1
		: static_cast<int>(lines.back().size()) + 1;
	return range;
}

std::string readTextFile(const std::filesystem::path & path) {
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return {};
	}
	return std::string(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
}

std::optional<std::filesystem::path> findCompilationDatabasePath(
	const std::string & rootPath) {
	if (trimCopy(rootPath).empty()) {
		return std::nullopt;
	}

	const std::filesystem::path root(rootPath);
	const std::vector<std::filesystem::path> candidates = {
		root / "compile_commands.json",
		root / "build" / "compile_commands.json",
		root / "out" / "build" / "compile_commands.json",
		root / "tests" / "build" / "compile_commands.json"
	};
	std::error_code ec;
	for (const auto & candidate : candidates) {
		if (std::filesystem::exists(candidate, ec) && !ec) {
			return candidate;
		}
	}
	return std::nullopt;
}

CompileCommandsIndex parseCompilationDatabase(
	const std::string & rootPath,
	const std::filesystem::path & dbPath) {
	CompileCommandsIndex index;
	index.path = dbPath.generic_string();
	const std::string text = readTextFile(dbPath);
	if (text.empty()) {
		return index;
	}

	static const std::regex filePattern(R"json("file"\s*:\s*"([^"]+)")json");
	const std::filesystem::path root(rootPath);
	for (std::sregex_iterator it(text.begin(), text.end(), filePattern), end;
		it != end; ++it) {
		std::filesystem::path filePath((*it)[1].str());
		if (!filePath.is_absolute()) {
			filePath = (dbPath.parent_path() / filePath).lexically_normal();
		}
		std::error_code ec;
		const auto relative = std::filesystem::relative(filePath, root, ec);
		if (!ec && !relative.empty()) {
			index.files.insert(relative.generic_string());
		} else {
			index.files.insert(filePath.lexically_normal().generic_string());
		}
	}
	return index;
}

std::vector<std::string> extractInvokedNames(const std::string & line) {
	std::vector<std::string> names;
	static const std::regex invokePattern(
		R"(\b([A-Za-z_]\w*)\s*\()"
	);
	std::unordered_set<std::string> seen;
	for (std::sregex_iterator it(line.begin(), line.end(), invokePattern), end;
		it != end; ++it) {
		const std::string name = (*it)[1].str();
		const std::string lowered = toLowerCopy(name);
		if (lowered == "if" || lowered == "for" || lowered == "while" ||
			lowered == "switch" || lowered == "return" || lowered == "sizeof" ||
			lowered == "catch") {
			continue;
		}
		if (seen.insert(lowered).second) {
			names.push_back(name);
		}
	}
	return names;
}

std::string normalizeContextFilePath(
	const ofxGgmlCodeAssistantContext & context,
	const std::string & path) {
	const std::string normalized = std::filesystem::path(path).generic_string();
	if (context.scriptSource == nullptr ||
		context.scriptSource->getSourceType() != ofxGgmlScriptSourceType::LocalFolder) {
		return normalized;
	}

	const std::filesystem::path root(context.scriptSource->getLocalFolderPath());
	std::error_code ec;
	const auto relative = std::filesystem::relative(std::filesystem::path(path), root, ec);
	if (!ec && !relative.empty()) {
		return relative.generic_string();
	}
	return normalized;
}

std::string buildUnifiedDiffForPatch(
	const ofxGgmlCodeAssistantPatchOperation & operation) {
	std::ostringstream diff;
	const std::string filePath = normalizeRelativeFilePath(operation.filePath);
	switch (operation.kind) {
	case ofxGgmlCodeAssistantPatchKind::WriteFile: {
		diff << "--- /dev/null\n";
		diff << "+++ b/" << filePath << "\n";
		const auto lines = splitLines(operation.content);
		diff << "@@ -0,0 +" << (lines.empty() ? 0 : static_cast<int>(lines.size()))
			<< " @@\n";
		if (lines.empty()) {
			diff << "+\n";
		}
		for (const auto & line : lines) {
			diff << "+" << line << "\n";
		}
		break;
	}
	case ofxGgmlCodeAssistantPatchKind::AppendText: {
		diff << "--- a/" << filePath << "\n";
		diff << "+++ b/" << filePath << "\n";
		const auto lines = splitLines(operation.content);
		diff << "@@ append @@\n";
		for (const auto & line : lines) {
			diff << "+" << line << "\n";
		}
		break;
	}
	case ofxGgmlCodeAssistantPatchKind::ReplaceTextOp: {
		diff << "--- a/" << filePath << "\n";
		diff << "+++ b/" << filePath << "\n";
		diff << "@@ replace @@\n";
		for (const auto & line : splitLines(operation.searchText)) {
			diff << "-" << line << "\n";
		}
		for (const auto & line : splitLines(operation.replacementText)) {
			diff << "+" << line << "\n";
		}
		break;
	}
	case ofxGgmlCodeAssistantPatchKind::DeleteFileOp: {
		diff << "--- a/" << filePath << "\n";
		diff << "+++ /dev/null\n";
		diff << "@@ delete @@\n";
		break;
	}
	}
	return diff.str();
}

bool loadFocusedFile(
	const ofxGgmlCodeAssistantContext & context,
	std::string * outName,
	std::string * outContent) {
	if (context.scriptSource == nullptr || context.focusedFileIndex < 0) {
		return false;
	}

	const auto files = context.scriptSource->getFiles();
	if (context.focusedFileIndex >= static_cast<int>(files.size())) {
		return false;
	}

	const auto & entry = files[static_cast<size_t>(context.focusedFileIndex)];
	if (entry.isDirectory) {
		return false;
	}

	std::string content;
	if (!context.scriptSource->loadFileContent(context.focusedFileIndex, content)) {
		return false;
	}

	if (outName != nullptr) {
		*outName = entry.name;
	}
	if (outContent != nullptr) {
		*outContent = truncateWithMarker(content, context.maxFocusedFileChars);
	}
	return true;
}

void appendRepoContext(
	std::ostringstream & prompt,
	const ofxGgmlCodeAssistantContext & context,
	bool * includedRepoContext,
	bool * includedFocusedFile,
	std::string * focusedFileName) {
	if (includedRepoContext != nullptr) {
		*includedRepoContext = false;
	}
	if (includedFocusedFile != nullptr) {
		*includedFocusedFile = false;
	}
	if (focusedFileName != nullptr) {
		focusedFileName->clear();
	}

	if (!context.includeRepoContext || context.scriptSource == nullptr) {
		return;
	}

	const auto sourceType = context.scriptSource->getSourceType();
	const auto files = context.scriptSource->getFiles();
	const auto workspaceInfo = context.scriptSource->getWorkspaceInfo();
	if (sourceType == ofxGgmlScriptSourceType::None || files.empty()) {
		return;
	}

	if (includedRepoContext != nullptr) {
		*includedRepoContext = true;
	}

	switch (sourceType) {
	case ofxGgmlScriptSourceType::LocalFolder:
		prompt << "Context: Loaded folder: "
			<< context.scriptSource->getLocalFolderPath() << "\n";
		if (workspaceInfo.hasVisualStudioSolution) {
			prompt << "Visual Studio solution: "
				<< workspaceInfo.visualStudioSolutionPath << "\n";
		}
		if (!workspaceInfo.visualStudioProjectPaths.empty()) {
			prompt << "Visual Studio projects: "
				<< workspaceInfo.visualStudioProjectPaths.size() << "\n";
		}
		if (workspaceInfo.hasCompilationDatabase) {
			prompt << "Compilation database: "
				<< workspaceInfo.compilationDatabasePath << "\n";
		}
		if (workspaceInfo.hasCMakeProject) {
			prompt << "CMake entrypoint: "
				<< workspaceInfo.cmakeListsPath << "\n";
		}
		if (workspaceInfo.hasOpenFrameworksProject) {
			prompt << "openFrameworks project marker: "
				<< workspaceInfo.addonsMakePath << "\n";
		}
		prompt << "Available files in this folder:\n";
		break;
	case ofxGgmlScriptSourceType::GitHubRepo:
		prompt << "Context: Loaded GitHub repository: "
			<< context.scriptSource->getGitHubOwnerRepo()
			<< " (branch: " << context.scriptSource->getGitHubBranch() << ")\n";
		prompt << "Available files in this repository:\n";
		break;
	case ofxGgmlScriptSourceType::Internet:
		prompt << "Context: Loaded internet sources:\n";
		break;
	default:
		break;
	}

	size_t listed = 0;
	for (const auto & entry : files) {
		if (entry.isDirectory) continue;
		prompt << "  - " << entry.name << "\n";
		++listed;
		if (listed >= context.maxRepoFiles) {
			const size_t remaining = files.size() > listed
				? files.size() - listed : 0;
			if (remaining > 0) {
				prompt << "  ... and " << remaining << " more files\n";
			}
			break;
		}
	}
	prompt << "\n";

	std::string selectedName;
	std::string selectedContent;
	if (loadFocusedFile(context, &selectedName, &selectedContent)) {
		if (focusedFileName != nullptr) {
			*focusedFileName = selectedName;
		}
		if (includedFocusedFile != nullptr) {
			*includedFocusedFile = true;
		}
		prompt << "Focused file: " << selectedName << "\n";
		prompt << "Focused file snippet:\n" << selectedContent << "\n\n";
	}
}

void appendStructuredResponseInstructions(std::ostringstream & prompt) {
	prompt << ofxGgmlCodeAssistant::buildStructuredResponseInstructions()
		<< "\n";
}

void appendAllowedFiles(
	std::ostringstream & prompt,
	const std::vector<std::string> & allowedFiles) {
	if (allowedFiles.empty()) {
		return;
	}
	prompt << "Allowed files for modifications:\n";
	for (const auto & file : allowedFiles) {
		prompt << "- " << normalizeRelativeFilePath(file) << "\n";
	}
	prompt << "Do not propose or modify any files outside this allow-list.\n\n";
}

void appendRequestConstraints(
	std::ostringstream & prompt,
	const ofxGgmlCodeAssistantRequest & request) {
	appendAllowedFiles(prompt, request.allowedFiles);

	if (!trimCopy(request.buildErrors).empty()) {
		prompt << "Build or test failure details:\n"
			<< request.buildErrors << "\n\n";
		const auto parsedErrors =
			ofxGgmlCodeAssistant::parseBuildErrors(request.buildErrors);
		if (!parsedErrors.empty()) {
			prompt << "Likely affected files from compiler output:\n";
			for (const auto & error : parsedErrors) {
				prompt << "- " << error.filePath;
				if (error.line > 0) {
					prompt << ":" << error.line;
				}
				if (!error.code.empty()) {
					prompt << " [" << error.code << "]";
				}
				if (!error.message.empty()) {
					prompt << " " << error.message;
				}
				prompt << "\n";
			}
			prompt << "\n";
		}
	}

	if (request.action == ofxGgmlCodeAssistantAction::Refactor) {
		prompt << "Refactor invariants:\n";
		prompt << "- Preserve the public API unless explicitly unavoidable.\n";
		if (request.updateTests) {
			prompt << "- Update or add tests to cover the refactor.\n";
		}
		if (request.forbidNewDependencies) {
			prompt << "- Do not introduce new third-party dependencies.\n";
		}
		prompt << "\n";
	}

	if (request.action == ofxGgmlCodeAssistantAction::Review) {
		prompt << "Return findings with explicit priority and confidence.\n\n";
	}

	if (request.action == ofxGgmlCodeAssistantAction::FixBuild) {
		prompt << "Focus on the minimal repair that makes the build or tests pass again.\n";
		prompt << "Collect likely affected files, propose concrete edits, and include verification commands.\n\n";
	}

	if (request.action == ofxGgmlCodeAssistantAction::GroundedDocs) {
		prompt << "Use grounded sources only. If sources are missing, say what additional docs are needed.\n\n";
	}
}

} // namespace

void ofxGgmlCodeAssistant::setCompletionExecutable(const std::string & path) {
	m_inference.setCompletionExecutable(path);
}

void ofxGgmlCodeAssistant::setEmbeddingExecutable(const std::string & path) {
	m_inference.setEmbeddingExecutable(path);
}

ofxGgmlInference & ofxGgmlCodeAssistant::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlCodeAssistant::getInference() const {
	return m_inference;
}

std::vector<ofxGgmlCodeLanguagePreset> ofxGgmlCodeAssistant::defaultLanguagePresets() {
	return {
		{"C++", ".cpp", "You are a C++ expert. Generate modern C++17 code."},
		{"Python", ".py", "You are a Python expert. Generate clean, idiomatic Python 3 code."},
		{"JavaScript", ".js", "You are a JavaScript expert. Generate modern ES6+ code."},
		{"Rust", ".rs", "You are a Rust expert. Generate safe, idiomatic Rust code."},
		{"GLSL", ".glsl", "You are a GLSL shader expert. Generate efficient GPU shader code."},
		{"Go", ".go", "You are a Go expert. Generate idiomatic Go code."},
		{"Bash", ".sh", "You are a Bash scripting expert. Generate portable shell scripts."},
		{"TypeScript", ".ts", "You are a TypeScript expert. Generate type-safe TypeScript code."}
	};
}

std::string ofxGgmlCodeAssistant::defaultActionBody(
	ofxGgmlCodeAssistantAction action,
	const std::string & userInput,
	bool hasFocusedFile,
	const std::string & lastTask,
	const std::string & lastOutput) {
	const std::string trimmedInput = trimCopy(userInput);
	const std::string trimmedTask = trimCopy(lastTask);
	const std::string trimmedOutput = trimCopy(lastOutput);

	auto withExtraInstructions = [&](const std::string & defaultForFile,
		const std::string & prefixForInput) {
		if (hasFocusedFile) {
			if (!trimmedInput.empty()) {
				return defaultForFile + "\n\nExtra instructions:\n" + trimmedInput;
			}
			return defaultForFile;
		}
		return prefixForInput + trimmedInput;
	};

	switch (action) {
	case ofxGgmlCodeAssistantAction::Ask:
		return trimmedInput;
	case ofxGgmlCodeAssistantAction::Generate:
		if (hasFocusedFile && trimmedInput.empty()) {
			return "Generate improved code for the focused file.";
		}
		return trimmedInput;
	case ofxGgmlCodeAssistantAction::Explain:
		return withExtraInstructions(
			"Explain the focused file code.",
			"Explain the following code:\n");
	case ofxGgmlCodeAssistantAction::Debug:
		return withExtraInstructions(
			"Find bugs in the focused file code.",
			"Find bugs in the following code:\n");
	case ofxGgmlCodeAssistantAction::Optimize:
		return withExtraInstructions(
			"Optimize the focused file code for performance. Show the improved version and explain what changed.",
			"Optimize the following code for performance. Show the improved version and explain what changed:\n");
	case ofxGgmlCodeAssistantAction::Edit:
		return withExtraInstructions(
			"Edit the focused file to satisfy the request. Keep unrelated code unchanged.",
			"Edit the following code to satisfy the request. Keep unrelated code unchanged:\n");
	case ofxGgmlCodeAssistantAction::Refactor:
		return withExtraInstructions(
			"Refactor the focused file code to improve readability, maintainability, and structure. Show the refactored version.",
			"Refactor the following code to improve readability, maintainability, and structure. Show the refactored version:\n");
	case ofxGgmlCodeAssistantAction::Review:
		return withExtraInstructions(
			"Review the focused file code for bugs, security issues, and style. Return findings with severity and suggested fixes.",
			"Review the following code for bugs, security issues, and style. Return findings with severity and suggested fixes:\n");
	case ofxGgmlCodeAssistantAction::FixBuild:
		if (!trimmedOutput.empty()) {
			return "Fix the build or test failure described below. Identify likely files, propose changes, and include verification commands.\n\n" +
				trimmedOutput;
		}
		return "Fix the build or test failure for this request. Identify likely files, propose changes, and include verification commands.\n\n" +
			trimmedInput;
	case ofxGgmlCodeAssistantAction::GroundedDocs:
		return "Answer the request using grounded documentation and source material only. Cite concrete supporting sources where possible.\n\n" +
			trimmedInput;
	case ofxGgmlCodeAssistantAction::ContinueTask: {
		std::string body =
			"Continue the task from the previous response. Keep the same intent and provide next concrete steps.\n\n";
		if (!trimmedOutput.empty()) {
			body += "Previous response:\n" + trimmedOutput;
		} else if (!trimmedTask.empty()) {
			body += "Previous task:\n" + trimmedTask;
		}
		return body;
	}
	case ofxGgmlCodeAssistantAction::Shorter: {
		std::string base = !trimmedTask.empty()
			? trimmedTask
			: std::string("Rewrite the previous response");
		base += "\n\nProvide a shorter answer. Keep only essential code and brief explanation.";
		return base;
	}
	case ofxGgmlCodeAssistantAction::MoreDetail: {
		std::string base = !trimmedTask.empty()
			? trimmedTask
			: std::string("Expand the previous response");
		base += "\n\nProvide a more detailed answer with reasoning, edge cases, and step-by-step implementation notes.";
		return base;
	}
	case ofxGgmlCodeAssistantAction::ContinueCutoff: {
		const std::string tail = !trimmedInput.empty() ? trimmedInput : trimmedOutput;
		return ofxGgmlInference::buildCutoffContinuationRequest(tail);
	}
	}

	return trimmedInput;
}

std::string ofxGgmlCodeAssistant::defaultActionLabel(
	ofxGgmlCodeAssistantAction action,
	const std::string & userInput,
	const std::string & focusedFileName) {
	const std::string trimmedInput = trimCopy(userInput);
	const bool hasFocusedFile = !trimCopy(focusedFileName).empty();

	auto appendInstructions = [&](std::string label) {
		if (!trimmedInput.empty()) {
			label += " Instructions: " + trimmedInput;
		}
		return label;
	};

	switch (action) {
	case ofxGgmlCodeAssistantAction::Ask:
		return trimmedInput;
	case ofxGgmlCodeAssistantAction::Generate:
		return appendInstructions(hasFocusedFile
			? "Generate focused file."
			: "Generate code.");
	case ofxGgmlCodeAssistantAction::Explain:
		return appendInstructions(hasFocusedFile
			? "Explain focused file."
			: "Explain code.");
	case ofxGgmlCodeAssistantAction::Debug:
		return appendInstructions(hasFocusedFile
			? "Debug focused file."
			: "Debug code.");
	case ofxGgmlCodeAssistantAction::Optimize:
		return appendInstructions(hasFocusedFile
			? "Optimize focused file."
			: "Optimize code.");
	case ofxGgmlCodeAssistantAction::Edit:
		return appendInstructions(hasFocusedFile
			? "Edit focused file."
			: "Edit code.");
	case ofxGgmlCodeAssistantAction::Refactor:
		return appendInstructions(hasFocusedFile
			? "Refactor focused file."
			: "Refactor code.");
	case ofxGgmlCodeAssistantAction::Review:
		return appendInstructions(hasFocusedFile
			? "Review focused file."
			: "Review code.");
	case ofxGgmlCodeAssistantAction::FixBuild:
		return "Fix build or test failure.";
	case ofxGgmlCodeAssistantAction::GroundedDocs:
		return appendInstructions("Answer with grounded docs.");
	case ofxGgmlCodeAssistantAction::ContinueTask:
		return "Continue the previous task.";
	case ofxGgmlCodeAssistantAction::Shorter:
		return "Provide a shorter answer for the previous task.";
	case ofxGgmlCodeAssistantAction::MoreDetail:
		return "Provide more detail for the previous task.";
	case ofxGgmlCodeAssistantAction::ContinueCutoff:
		return "Continue from cutoff.";
	}

	return trimmedInput;
}

std::vector<ofxGgmlCodeAssistantSymbol> ofxGgmlCodeAssistant::extractSymbols(
	const std::string & text,
	const std::string & filePath) {
	std::vector<ofxGgmlCodeAssistantSymbol> symbols;
	const auto lines = splitLines(text);

	static const std::regex cppFunction(
		R"(^\s*(?:template\s*<[^>]+>\s*)?(?:[\w:&*<>\[\],~]+\s+)+((?:(?:[A-Za-z_]\w*)::)*)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\b)?\s*(?:\{|$))");
	static const std::regex cppType(
		R"(^\s*(class|struct|enum|namespace)\s+([A-Za-z_]\w*))");
	static const std::regex pythonDecl(
		R"(^\s*(def|class)\s+([A-Za-z_]\w*))");
	static const std::regex jsDecl(
		R"(^\s*(function|class)\s+([A-Za-z_]\w*))");
	static const std::regex assignDecl(
		R"(^\s*(?:const|let|var|auto)\s+([A-Za-z_]\w*)\s*=\s*(?:\(|\[|async\b))");

	std::vector<std::pair<int, std::string>> lexicalScopes;
	int braceDepth = 0;
	for (size_t i = 0; i < lines.size(); ++i) {
		const std::string line = trimCopy(lines[i]);
		if (line.empty()) {
			int openCount = static_cast<int>(std::count(lines[i].begin(), lines[i].end(), '{'));
			int closeCount = static_cast<int>(std::count(lines[i].begin(), lines[i].end(), '}'));
			braceDepth += openCount - closeCount;
			while (!lexicalScopes.empty() && lexicalScopes.back().first > braceDepth) {
				lexicalScopes.pop_back();
			}
			continue;
		}

		std::smatch match;
		ofxGgmlCodeAssistantSymbol symbol;
		symbol.filePath = filePath;
		symbol.line = static_cast<int>(i + 1);
		symbol.signature = line;
		symbol.preview = line;
		symbol.semanticBackend = "scope_graph";
		symbol.containerName = joinScope([&]() {
			std::vector<std::string> scopeNames;
			scopeNames.reserve(lexicalScopes.size());
			for (const auto & scope : lexicalScopes) {
				scopeNames.push_back(scope.second);
			}
			return scopeNames;
		}());

		if (std::regex_search(line, match, cppType) && match.size() >= 3) {
			symbol.kind = match[1].str();
			symbol.name = match[2].str();
			symbol.isDefinition = lines[i].find('{') != std::string::npos;
			symbol.qualifiedName = symbol.containerName.empty()
				? symbol.name
				: symbol.containerName + "::" + symbol.name;
			symbol.range = estimateBraceRange(lines, i);
		} else if (std::regex_search(line, match, cppFunction) && match.size() >= 3) {
			symbol.kind = "function";
			const std::string qualifier = trimCopy(match[1].str());
			symbol.name = match[2].str();
			if (!qualifier.empty()) {
				const std::string normalizedQualifier =
					qualifier.size() >= 2 && qualifier.substr(qualifier.size() - 2) == "::"
					? qualifier.substr(0, qualifier.size() - 2)
					: qualifier;
				symbol.containerName = normalizedQualifier;
				symbol.qualifiedName = normalizedQualifier + "::" + symbol.name;
			} else {
				symbol.qualifiedName = symbol.containerName.empty()
					? symbol.name
					: symbol.containerName + "::" + symbol.name;
			}
			symbol.isDefinition = lines[i].find('{') != std::string::npos;
			symbol.range = estimateBraceRange(lines, i);
		} else if (std::regex_search(line, match, pythonDecl) && match.size() >= 3) {
			symbol.kind = match[1].str() == "def" ? "function" : "class";
			symbol.name = match[2].str();
			symbol.isDefinition = true;
			symbol.qualifiedName = symbol.containerName.empty()
				? symbol.name
				: symbol.containerName + "::" + symbol.name;
			symbol.range = estimateIndentRange(lines, i);
		} else if (std::regex_search(line, match, jsDecl) && match.size() >= 3) {
			symbol.kind = match[1].str() == "function" ? "function" : "class";
			symbol.name = match[2].str();
			symbol.isDefinition = lines[i].find('{') != std::string::npos;
			symbol.qualifiedName = symbol.containerName.empty()
				? symbol.name
				: symbol.containerName + "::" + symbol.name;
			symbol.range = estimateBraceRange(lines, i);
		} else if (std::regex_search(line, match, assignDecl) && match.size() >= 2) {
			symbol.kind = "binding";
			symbol.name = match[1].str();
			symbol.isDefinition = true;
			symbol.qualifiedName = symbol.containerName.empty()
				? symbol.name
				: symbol.containerName + "::" + symbol.name;
			symbol.range.startLine = static_cast<int>(i + 1);
			symbol.range.startColumn = 1;
			symbol.range.endLine = static_cast<int>(i + 1);
			symbol.range.endColumn = static_cast<int>(lines[i].size()) + 1;
		} else {
			int openCount = static_cast<int>(std::count(lines[i].begin(), lines[i].end(), '{'));
			int closeCount = static_cast<int>(std::count(lines[i].begin(), lines[i].end(), '}'));
			braceDepth += openCount - closeCount;
			while (!lexicalScopes.empty() && lexicalScopes.back().first > braceDepth) {
				lexicalScopes.pop_back();
			}
			continue;
		}

		symbols.push_back(std::move(symbol));

		const int openCount = static_cast<int>(std::count(lines[i].begin(), lines[i].end(), '{'));
		const int closeCount = static_cast<int>(std::count(lines[i].begin(), lines[i].end(), '}'));
		if ((symbols.back().kind == "class" || symbols.back().kind == "struct" ||
			symbols.back().kind == "namespace") &&
			openCount > closeCount) {
			lexicalScopes.emplace_back(braceDepth + openCount, symbols.back().name);
		}
		braceDepth += openCount - closeCount;
		while (!lexicalScopes.empty() && lexicalScopes.back().first > braceDepth) {
			lexicalScopes.pop_back();
		}
	}

	return symbols;
}

std::vector<ofxGgmlCodeAssistantSymbol> ofxGgmlCodeAssistant::retrieveSymbols(
	const std::string & query,
	const ofxGgmlCodeAssistantContext & context) const {
	std::vector<ofxGgmlCodeAssistantSymbol> ranked;
	if (context.scriptSource == nullptr || !context.includeSymbolContext) {
		return ranked;
	}
	const auto queryTokens = tokenizeQuery(query);
	auto index = buildSemanticIndex(context);
	if (index.symbols.empty()) {
		return ranked;
	}

	for (auto & symbol : index.symbols) {
		const std::string lowerName = toLowerCopy(symbol.name);
		const std::string lowerQualified = toLowerCopy(symbol.qualifiedName);
		const std::string lowerContainer = toLowerCopy(symbol.containerName);
		const std::string lowerSig = toLowerCopy(symbol.signature);
		const std::string lowerFile = toLowerCopy(symbol.filePath);
		const auto nameTokens = tokenizeIdentifier(symbol.name);
		const auto qualifiedTokens = tokenizeIdentifier(symbol.qualifiedName);
		const auto containerTokens = tokenizeIdentifier(symbol.containerName);
		const auto signatureTokens = tokenizeIdentifier(symbol.signature);
		const auto fileTokens = tokenizeIdentifier(symbol.filePath);
		float score = 0.0f;

		if (context.focusedFileIndex >= 0) {
			const auto currentFiles = context.scriptSource->getFiles();
			if (context.focusedFileIndex < static_cast<int>(currentFiles.size()) &&
				currentFiles[static_cast<size_t>(context.focusedFileIndex)].name ==
					symbol.filePath) {
				score += 0.75f;
			}
		}

		for (const auto & token : queryTokens) {
			if (lowerName == token) {
				score += 4.0f;
			}
			if (containsToken(nameTokens, token)) {
				score += 5.0f;
			} else if (lowerName.find(token) != std::string::npos) {
				score += 2.25f;
			}
			if (containsToken(qualifiedTokens, token)) {
				score += 2.0f;
			}
			if (lowerQualified.find(token) != std::string::npos) {
				score += 1.5f;
			}
			if (containsToken(containerTokens, token)) {
				score += 1.0f;
			}
			if (lowerContainer.find(token) != std::string::npos) {
				score += 0.75f;
			}
			if (containsToken(signatureTokens, token)) {
				score += 1.5f;
			}
			if (lowerSig.find(token) != std::string::npos) {
				score += 1.0f;
			}
			if (containsToken(fileTokens, token)) {
				score += 1.0f;
			}
			if (lowerFile.find(token) != std::string::npos) {
				score += 0.75f;
			}
		}

		if (score <= 0.0f) {
			score = 0.15f;
		}
		if (!symbol.semanticBackend.empty() &&
			symbol.semanticBackend.find("compilation_database") != std::string::npos) {
			score += 0.5f;
		}
		symbol.score = score;
	}

	std::sort(index.symbols.begin(), index.symbols.end(),
		[](const ofxGgmlCodeAssistantSymbol & a,
			const ofxGgmlCodeAssistantSymbol & b) {
			if (a.score != b.score) {
				return a.score > b.score;
			}
			if (a.filePath != b.filePath) {
				return a.filePath < b.filePath;
			}
			return a.line < b.line;
		});

	if (index.symbols.size() > context.maxSymbols) {
		index.symbols.resize(context.maxSymbols);
	}

	for (auto & symbol : index.symbols) {
		if (symbol.references.size() > context.maxSymbolReferences) {
			symbol.references.resize(context.maxSymbolReferences);
		}
		ranked.push_back(std::move(symbol));
	}

	return ranked;
}

ofxGgmlCodeAssistantSymbolContext ofxGgmlCodeAssistant::buildSymbolContext(
	const ofxGgmlCodeAssistantSymbolQuery & query,
	const ofxGgmlCodeAssistantContext & context) const {
	ofxGgmlCodeAssistantSymbolContext symbolContext;
	symbolContext.query = !trimCopy(query.query).empty()
		? query.query
		: joinStrings(query.targetSymbols, ", ");
	symbolContext.includesCallers = query.includeCallers;
	auto semanticIndex = buildSemanticIndex(context);
	if (semanticIndex.symbols.empty()) {
		return symbolContext;
	}

	std::vector<ofxGgmlCodeAssistantSymbol> candidates = retrieveSymbols(
		symbolContext.query,
		context);
	const auto preferredSymbols = [&]() {
		std::vector<std::string> lowered;
		lowered.reserve(query.targetSymbols.size());
		for (const auto & symbol : query.targetSymbols) {
			lowered.push_back(toLowerCopy(symbol));
		}
		return lowered;
	}();

	for (auto & symbol : candidates) {
		if (symbolContext.definitions.size() >= query.maxDefinitions) {
			break;
		}
		if (!preferredSymbols.empty()) {
			const std::string loweredName = toLowerCopy(symbol.name);
			if (std::find(preferredSymbols.begin(), preferredSymbols.end(),
					loweredName) == preferredSymbols.end()) {
				bool containsPreferred = false;
				for (const auto & preferred : preferredSymbols) {
					if (loweredName.find(preferred) != std::string::npos ||
						preferred.find(loweredName) != std::string::npos) {
						containsPreferred = true;
						break;
					}
				}
				if (!containsPreferred) {
					continue;
				}
			}
		}

		if (query.includeDefinitions) {
			symbolContext.definitions.push_back(symbol);
		}
		if (!query.includeReferences && !query.includeCallers) {
			continue;
		}

		size_t collected = 0;
		for (const auto & reference : symbol.references) {
			if (symbolContext.relatedReferences.size() >= query.maxReferences) {
				break;
			}
			if (reference.kind == "caller" && !query.includeCallers) {
				continue;
			}
			if (reference.kind != "caller" && !query.includeReferences) {
				continue;
			}
			symbolContext.relatedReferences.push_back(reference);
			++collected;
			if (collected >= query.maxReferences) {
				break;
			}
		}
	}

	if (query.includeCallers && symbolContext.relatedReferences.empty()) {
		for (const auto & caller : semanticIndex.callers) {
			if (symbolContext.relatedReferences.size() >= query.maxReferences) {
				break;
			}
			for (const auto & preferred : preferredSymbols) {
				if (toLowerCopy(caller.preview).find(preferred) != std::string::npos) {
					symbolContext.relatedReferences.push_back(caller);
					break;
				}
			}
		}
	}

	return symbolContext;
}

ofxGgmlCodeAssistantSemanticIndex ofxGgmlCodeAssistant::buildSemanticIndex(
	const ofxGgmlCodeAssistantContext & context) const {
	ofxGgmlCodeAssistantSemanticIndex index;
	if (context.scriptSource == nullptr) {
		return index;
	}

	const auto files = context.scriptSource->getFiles();
	if (files.empty()) {
		return index;
	}

	CompileCommandsIndex compileCommands;
	if (context.scriptSource->getSourceType() == ofxGgmlScriptSourceType::LocalFolder) {
		if (const auto dbPath = findCompilationDatabasePath(
				context.scriptSource->getLocalFolderPath())) {
			compileCommands = parseCompilationDatabase(
				context.scriptSource->getLocalFolderPath(),
				*dbPath);
			index.hasCompilationDatabase = !compileCommands.files.empty();
			index.compilationDatabasePath = compileCommands.path;
		}
	}
	index.backendName = index.hasCompilationDatabase
		? "compilation_database+scope_graph"
		: "scope_graph";

	std::unordered_map<std::string, std::vector<std::string>> fileLines;
	std::unordered_map<std::string, std::vector<std::string>> fileBodies;
	for (size_t i = 0; i < files.size(); ++i) {
		const auto & entry = files[i];
		if (entry.isDirectory) {
			continue;
		}

		std::string content;
		if (!context.scriptSource->loadFileContent(static_cast<int>(i), content)) {
			continue;
		}

		const std::string normalizedFile = normalizeContextFilePath(context, entry.name);
		fileLines[normalizedFile] = splitLines(content);
		fileBodies[normalizedFile] = splitLines(content);
		auto symbols = extractSymbols(content, normalizedFile);
		for (auto & symbol : symbols) {
			if (index.hasCompilationDatabase &&
				compileCommands.files.find(normalizedFile) != compileCommands.files.end()) {
				symbol.semanticBackend = "compilation_database+scope_graph";
			}
			index.symbols.push_back(std::move(symbol));
		}
	}

	std::unordered_map<std::string, std::vector<size_t>> symbolsByName;
	for (size_t i = 0; i < index.symbols.size(); ++i) {
		symbolsByName[toLowerCopy(index.symbols[i].name)].push_back(i);
		if (!index.symbols[i].qualifiedName.empty()) {
			symbolsByName[toLowerCopy(index.symbols[i].qualifiedName)].push_back(i);
		}
	}

	std::set<std::tuple<std::string, int, std::string>> seenReferenceKeys;
	for (size_t symbolIndex = 0; symbolIndex < index.symbols.size(); ++symbolIndex) {
		auto & callerSymbol = index.symbols[symbolIndex];
		if (callerSymbol.kind != "function") {
			continue;
		}
		const auto linesIt = fileBodies.find(callerSymbol.filePath);
		if (linesIt == fileBodies.end()) {
			continue;
		}
		const auto & lines = linesIt->second;
		const int startLine = (std::max)(1, callerSymbol.range.startLine);
		const int endLine = callerSymbol.range.endLine > 0
			? callerSymbol.range.endLine
			: callerSymbol.line;
		for (int lineNumber = startLine; lineNumber <= endLine &&
			lineNumber <= static_cast<int>(lines.size()); ++lineNumber) {
			const std::string trimmed = trimCopy(lines[static_cast<size_t>(lineNumber - 1)]);
			if (trimmed.empty()) {
				continue;
			}
			for (const auto & invoked : extractInvokedNames(trimmed)) {
				const auto targetIt = symbolsByName.find(toLowerCopy(invoked));
				if (targetIt == symbolsByName.end()) {
					continue;
				}
				for (size_t targetIndex : targetIt->second) {
					auto & callee = index.symbols[targetIndex];
					if (callee.qualifiedName == callerSymbol.qualifiedName &&
						callee.filePath == callerSymbol.filePath) {
						continue;
					}
					const auto key = std::make_tuple(
						callee.filePath,
						lineNumber,
						callerSymbol.qualifiedName + "->" + callee.qualifiedName);
					if (!seenReferenceKeys.insert(key).second) {
						continue;
					}
					ofxGgmlCodeAssistantSymbolReference reference;
					reference.kind = "caller";
					reference.filePath = callerSymbol.filePath;
					reference.line = lineNumber;
					reference.preview = trimmed;
					reference.callerSymbol = callerSymbol.qualifiedName;
					reference.targetSymbol = callee.qualifiedName;
					reference.range.startLine = lineNumber;
					reference.range.startColumn = 1;
					reference.range.endLine = lineNumber;
					reference.range.endColumn = static_cast<int>(trimmed.size()) + 1;
					callee.references.push_back(reference);
					index.callers.push_back(reference);
				}
			}
		}
	}

	for (auto & symbol : index.symbols) {
		for (const auto & fileEntry : fileLines) {
			const auto & lines = fileEntry.second;
			for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
				const int currentLine = static_cast<int>(lineIndex + 1);
				if (fileEntry.first == symbol.filePath &&
					currentLine >= symbol.range.startLine &&
					currentLine <= symbol.range.endLine) {
					continue;
				}
				if (lines[lineIndex].find(symbol.name) == std::string::npos) {
					continue;
				}
				const auto referenceKey = std::make_tuple(
					fileEntry.first,
					currentLine,
					symbol.qualifiedName);
				if (!seenReferenceKeys.insert(referenceKey).second) {
					continue;
				}

				ofxGgmlCodeAssistantSymbolReference reference;
				reference.kind = isLikelyCallerLine(trimCopy(lines[lineIndex]), symbol.name)
					? "caller"
					: "reference";
				reference.filePath = fileEntry.first;
				reference.line = currentLine;
				reference.preview = trimCopy(lines[lineIndex]);
				reference.targetSymbol = symbol.qualifiedName;
				reference.range.startLine = currentLine;
				reference.range.startColumn = 1;
				reference.range.endLine = currentLine;
				reference.range.endColumn =
					static_cast<int>(reference.preview.size()) + 1;
				symbol.references.push_back(reference);
				if (reference.kind == "caller") {
					index.callers.push_back(reference);
				}
			}
		}
	}

	return index;
}

std::string ofxGgmlCodeAssistant::buildStructuredResponseInstructions() {
	return
		"Return a structured plan using one item per line with these tags:\n"
		"GOAL: short summary\n"
		"APPROACH: short summary\n"
		"STEP: concrete next step\n"
		"FILE: relative/path | why it matters | comma,separated,symbols\n"
		"PATCH: write|replace|append|delete | relative/path | short summary\n"
		"SEARCH: escaped single-line search text for the previous PATCH when using replace\n"
		"REPLACE: escaped single-line replacement text for the previous PATCH when using replace\n"
		"CONTENT: escaped single-line file content for write/append patches (use \\n for newlines)\n"
		"DIFF: escaped unified diff text when you can express the change as a unified diff\n"
		"COMMAND: label | cwd | executable | arg1 | arg2 ...\n"
		"EXPECT: expected verification outcome for the previous COMMAND\n"
		"RETRY: true|false for the previous COMMAND\n"
		"FINDING: priority | confidence | relative/path | line | title\n"
		"DETAIL: description for the previous FINDING\n"
		"FIX: concrete fix suggestion for the previous FINDING\n"
		"RISK: possible risk or regression\n"
		"QUESTION: unresolved question\n"
		"Only use escaped single-line values for SEARCH, REPLACE, CONTENT, and DIFF.";
}

std::string ofxGgmlCodeAssistant::buildUnifiedDiffFromStructuredResult(
	const ofxGgmlCodeAssistantStructuredResult & structured) {
	if (!trimCopy(structured.unifiedDiff).empty()) {
		return structured.unifiedDiff;
	}

	std::ostringstream diff;
	for (const auto & operation : structured.patchOperations) {
		diff << buildUnifiedDiffForPatch(operation);
		if (diff.tellp() > 0 && diff.str().back() != '\n') {
			diff << "\n";
		}
	}
	return diff.str();
}

std::vector<ofxGgmlCodeAssistantBuildError> ofxGgmlCodeAssistant::parseBuildErrors(
	const std::string & text) {
	std::vector<ofxGgmlCodeAssistantBuildError> errors;
	static const std::regex msvcPattern(
		R"(^(.+)\((\d+)(?:,(\d+))?\):\s*(fatal error|error|warning)\s+([A-Za-z]+\d+):\s*(.+)$)");

	for (const auto & rawLine : splitLines(text)) {
		const std::string line = trimCopy(rawLine);
		if (line.empty()) {
			continue;
		}

		std::smatch match;
		ofxGgmlCodeAssistantBuildError error;
		error.rawLine = line;
		if (std::regex_match(line, match, msvcPattern) && match.size() >= 7) {
			error.filePath = match[1].str();
			error.line = std::stoi(match[2].str());
			if (match[3].matched) {
				error.column = std::stoi(match[3].str());
			}
			error.code = match[5].str();
			error.message = match[6].str();
			errors.push_back(std::move(error));
			continue;
		}

		std::string severity;
		std::string severityToken;
		std::size_t severityPos = std::string::npos;
		for (const auto & candidate : std::vector<std::pair<std::string, std::string>>{
				 {": fatal error:", "fatal error"},
				 {": error:", "error"},
				 {": warning:", "warning"}}) {
			severityPos = line.find(candidate.first);
			if (severityPos != std::string::npos) {
				severityToken = candidate.first;
				severity = candidate.second;
				break;
			}
		}
		if (severityPos == std::string::npos) {
			continue;
		}

		const std::string locationPart = trimCopy(line.substr(0, severityPos));
		const std::size_t lastColon = locationPart.rfind(':');
		if (lastColon == std::string::npos) {
			continue;
		}

		auto isDigits = [](const std::string & value) {
			return !value.empty() &&
				std::all_of(value.begin(), value.end(), [](unsigned char ch) {
					return std::isdigit(ch) != 0;
				});
		};

		std::string lineText;
		std::string columnText;
		std::string filePath;
		const std::string tailText = trimCopy(locationPart.substr(lastColon + 1));
		if (!isDigits(tailText)) {
			continue;
		}

		const std::string beforeLast = trimCopy(locationPart.substr(0, lastColon));
		const std::size_t secondLastColon = beforeLast.rfind(':');
		if (secondLastColon != std::string::npos) {
			const std::string maybeLine = trimCopy(beforeLast.substr(secondLastColon + 1));
			const std::string maybeFile = trimCopy(beforeLast.substr(0, secondLastColon));
			if (isDigits(maybeLine) && !maybeFile.empty()) {
				filePath = maybeFile;
				lineText = maybeLine;
				columnText = tailText;
			}
		}
		if (lineText.empty()) {
			filePath = beforeLast;
			lineText = tailText;
		}
		if (filePath.empty() || !isDigits(lineText)) {
			continue;
		}

		error.filePath = trimCopy(filePath);
		error.line = std::stoi(lineText);
		if (isDigits(columnText)) {
			error.column = std::stoi(columnText);
		}
		error.code = severity;
		error.message = trimCopy(line.substr(severityPos + severityToken.size()));
		errors.push_back(std::move(error));
	}

	return errors;
}

ofxGgmlCodeAssistantStructuredResult ofxGgmlCodeAssistant::parseStructuredResult(
	const std::string & text) {
	ofxGgmlCodeAssistantStructuredResult structured;
	ofxGgmlCodeAssistantPatchOperation * currentPatch = nullptr;
	ofxGgmlCodeAssistantCommandSuggestion * currentCommand = nullptr;
	ofxGgmlCodeAssistantReviewFinding * currentFinding = nullptr;

	for (const auto & rawLine : splitLines(text)) {
		const std::string line = trimCopy(rawLine);
		if (line.empty()) {
			continue;
		}

		auto consumeValue = [&](const std::string & prefix) {
			return trimCopy(line.substr(prefix.size()));
		};

		if (line.rfind("GOAL:", 0) == 0) {
			structured.goalSummary = unescapeTaggedValue(consumeValue("GOAL:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("APPROACH:", 0) == 0) {
			structured.approachSummary = unescapeTaggedValue(
				consumeValue("APPROACH:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("STEP:", 0) == 0) {
			structured.steps.push_back(unescapeTaggedValue(
				consumeValue("STEP:")));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("FILE:", 0) == 0) {
			const auto fields = splitPipeFields(consumeValue("FILE:"));
			if (!fields.empty()) {
				ofxGgmlCodeAssistantFileIntent fileIntent;
				fileIntent.filePath = fields[0];
				if (fields.size() >= 2) {
					fileIntent.reason = unescapeTaggedValue(fields[1]);
				}
				if (fields.size() >= 3) {
					for (const auto & symbol : splitPipeFields(
						std::regex_replace(fields[2], std::regex(","), "|"))) {
						if (!symbol.empty()) {
							fileIntent.symbols.push_back(symbol);
						}
					}
				}
				structured.filesToTouch.push_back(std::move(fileIntent));
				structured.detectedStructuredOutput = true;
			}
			continue;
		}
		if (line.rfind("PATCH:", 0) == 0) {
			const auto fields = splitPipeFields(consumeValue("PATCH:"));
			if (fields.size() >= 2) {
				ofxGgmlCodeAssistantPatchOperation operation;
				operation.kind = parsePatchKind(fields[0]);
				operation.filePath = fields[1];
				if (fields.size() >= 3) {
					operation.summary = unescapeTaggedValue(fields[2]);
				}
				structured.patchOperations.push_back(std::move(operation));
				currentPatch = &structured.patchOperations.back();
				currentCommand = nullptr;
				currentFinding = nullptr;
				structured.detectedStructuredOutput = true;
			}
			continue;
		}
		if (line.rfind("SEARCH:", 0) == 0 && currentPatch != nullptr) {
			currentPatch->searchText = unescapeTaggedValue(
				consumeValue("SEARCH:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("REPLACE:", 0) == 0 && currentPatch != nullptr) {
			currentPatch->replacementText = unescapeTaggedValue(
				consumeValue("REPLACE:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("CONTENT:", 0) == 0 && currentPatch != nullptr) {
			currentPatch->content = unescapeTaggedValue(
				consumeValue("CONTENT:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("DIFF:", 0) == 0) {
			structured.unifiedDiff = unescapeTaggedValue(
				consumeValue("DIFF:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("COMMAND:", 0) == 0) {
			const auto fields = splitPipeFields(consumeValue("COMMAND:"));
			if (!fields.empty()) {
				ofxGgmlCodeAssistantCommandSuggestion command;
				command.label = fields[0];
				if (fields.size() >= 3) {
					command.workingDirectory = fields[1];
					command.executable = fields[2];
					for (size_t i = 3; i < fields.size(); ++i) {
						command.arguments.push_back(unescapeTaggedValue(fields[i]));
					}
				} else if (fields.size() == 2) {
					command.workingDirectory = fields[1];
					command.executable = fields[0];
				} else {
					command.executable = fields[0];
				}
				if (trimCopy(command.executable).empty()) {
					command.executable = command.label;
				}
				structured.verificationCommands.push_back(std::move(command));
				currentCommand = &structured.verificationCommands.back();
				currentPatch = nullptr;
				currentFinding = nullptr;
				structured.detectedStructuredOutput = true;
			}
			continue;
		}
		if (line.rfind("EXPECT:", 0) == 0 && currentCommand != nullptr) {
			currentCommand->expectedOutcome = unescapeTaggedValue(
				consumeValue("EXPECT:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("RETRY:", 0) == 0 && currentCommand != nullptr) {
			currentCommand->retryOnFailure =
				toLowerCopy(consumeValue("RETRY:")) == "true";
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("FINDING:", 0) == 0) {
			const auto fields = splitPipeFields(consumeValue("FINDING:"));
			if (fields.size() >= 5) {
				ofxGgmlCodeAssistantReviewFinding finding;
				try {
					finding.priority = std::stoi(fields[0]);
				} catch (...) {
					finding.priority = 2;
				}
				try {
					finding.confidence = std::stof(fields[1]);
				} catch (...) {
					finding.confidence = 0.0f;
				}
				finding.filePath = fields[2];
				try {
					finding.line = std::stoi(fields[3]);
				} catch (...) {
					finding.line = 0;
				}
				finding.title = unescapeTaggedValue(fields[4]);
				structured.reviewFindings.push_back(std::move(finding));
				currentFinding = &structured.reviewFindings.back();
				currentPatch = nullptr;
				currentCommand = nullptr;
				structured.detectedStructuredOutput = true;
			}
			continue;
		}
		if (line.rfind("DETAIL:", 0) == 0 && currentFinding != nullptr) {
			currentFinding->description = unescapeTaggedValue(
				consumeValue("DETAIL:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("FIX:", 0) == 0 && currentFinding != nullptr) {
			currentFinding->fixSuggestion = unescapeTaggedValue(
				consumeValue("FIX:"));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("RISK:", 0) == 0) {
			structured.risks.push_back(unescapeTaggedValue(
				consumeValue("RISK:")));
			structured.detectedStructuredOutput = true;
			continue;
		}
		if (line.rfind("QUESTION:", 0) == 0) {
			structured.questions.push_back(unescapeTaggedValue(
				consumeValue("QUESTION:")));
			structured.detectedStructuredOutput = true;
			continue;
		}
	}

	return structured;
}

ofxGgmlCodeAssistantPreparedPrompt ofxGgmlCodeAssistant::preparePrompt(
	const ofxGgmlCodeAssistantRequest & request,
	const ofxGgmlCodeAssistantContext & context) const {
	ofxGgmlCodeAssistantPreparedPrompt prepared;

	std::string focusedFileName;
	std::string focusedFileContent;
	const bool hasFocusedFile = loadFocusedFile(
		context,
		&focusedFileName,
		&focusedFileContent);

	prepared.body = trimCopy(request.bodyOverride);
	if (prepared.body.empty()) {
		prepared.body = defaultActionBody(
			request.action,
			request.userInput,
			hasFocusedFile,
			request.lastTask,
			request.lastOutput);
	}

	prepared.requestLabel = trimCopy(request.labelOverride);
	if (prepared.requestLabel.empty()) {
		prepared.requestLabel = defaultActionLabel(
			request.action,
			request.userInput,
			focusedFileName);
		if (prepared.requestLabel.empty()) {
			prepared.requestLabel = prepared.body;
		}
	}

	prepared.focusedFileName = focusedFileName;
	prepared.requestsStructuredResult = request.requestStructuredResult;
	prepared.requestedUnifiedDiff = request.requestUnifiedDiff;

	ofxGgmlCodeAssistantSymbolQuery symbolQuery = request.symbolQuery;
	if (trimCopy(symbolQuery.query).empty()) {
		symbolQuery.query = prepared.body;
	}
	prepared.retrievedSymbolContext = buildSymbolContext(symbolQuery, context);
	prepared.retrievedSymbols = prepared.retrievedSymbolContext.definitions;
	prepared.includedSymbolContext =
		!prepared.retrievedSymbolContext.definitions.empty() ||
		!prepared.retrievedSymbolContext.relatedReferences.empty();

	std::ostringstream prompt;
	if (!trimCopy(request.language.systemPrompt).empty()) {
		prompt << request.language.systemPrompt << "\n";
	}
	if (context.projectMemory != nullptr) {
		prompt << context.projectMemory->buildPromptContext(
			context.projectMemoryHeading);
	}

	appendRepoContext(
		prompt,
		context,
		&prepared.includedRepoContext,
		&prepared.includedFocusedFile,
		&prepared.focusedFileName);

	appendRequestConstraints(prompt, request);

	if (!prepared.retrievedSymbolContext.definitions.empty() ||
		!prepared.retrievedSymbolContext.relatedReferences.empty()) {
		prompt << "Relevant symbols for this request:\n";
		for (const auto & symbol : prepared.retrievedSymbolContext.definitions) {
			std::ostringstream scoreStream;
			scoreStream.setf(std::ios::fixed);
			scoreStream.precision(2);
			scoreStream << symbol.score;
			prompt << "- " << symbol.name << " (" << symbol.kind << ") "
				<< symbol.filePath << ":" << symbol.line
				<< " score=" << scoreStream.str() << "\n";
			if (!symbol.signature.empty()) {
				prompt << "  Signature: " << symbol.signature << "\n";
			}
		}
		for (const auto & ref : prepared.retrievedSymbolContext.relatedReferences) {
			prompt << "  " << (ref.kind.empty() ? "Reference" : ref.kind)
				<< ": " << ref.filePath << ":" << ref.line
				<< " " << ref.preview << "\n";
		}
		prompt << "\n";
	}

	if (!request.webUrls.empty()) {
		prompt << "Grounded web/doc sources requested:\n";
		for (const auto & url : request.webUrls) {
			prompt << "- " << url << "\n";
		}
		prompt << "\n";
	}

	if (request.requestStructuredResult) {
		appendStructuredResponseInstructions(prompt);
		if (request.requestUnifiedDiff) {
			prompt << "Prefer including a DIFF: entry with a unified diff in addition to file operations when practical.\n\n";
		}
	}

	if (!request.requestStructuredResult && request.requestUnifiedDiff) {
		prompt << "When proposing changes, include a unified diff.\n\n";
	}

	if (request.action == ofxGgmlCodeAssistantAction::Edit &&
		!request.allowedFiles.empty()) {
		prompt << "This is a constrained edit request. Touch only the allowed files.\n\n";
	}

	if (request.action == ofxGgmlCodeAssistantAction::Refactor &&
		request.preservePublicApi) {
		prompt << "Preserve the existing public API surface.\n\n";
	}

	if (request.action == ofxGgmlCodeAssistantAction::GroundedDocs) {
		prompt << "If the answer depends on documentation, prefer citations over guesses.\n\n";
	}

	prompt << "Generate high-quality code and short explanation for this request:\n"
		<< prepared.body << "\n\nAnswer:\n";
	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlCodeAssistantResult ofxGgmlCodeAssistant::run(
	const std::string & modelPath,
	const ofxGgmlCodeAssistantRequest & request,
	const ofxGgmlCodeAssistantContext & context,
	const ofxGgmlInferenceSettings & inferenceSettings,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlCodeAssistantResult result;
	result.prepared = preparePrompt(request, context);

	std::vector<ofxGgmlPromptSource> sources;
	if (context.attachScriptSourceDocuments && context.scriptSource != nullptr) {
		const auto docs = ofxGgmlInference::collectScriptSourceDocuments(
			*context.scriptSource,
			sourceSettings);
		sources.insert(sources.end(), docs.begin(), docs.end());
	}
	if (!request.webUrls.empty()) {
		const auto webSources = ofxGgmlInference::fetchUrlSources(
			request.webUrls,
			sourceSettings);
		sources.insert(sources.end(), webSources.begin(), webSources.end());
	}

	if (!sources.empty()) {
		result.inference = m_inference.generateWithSources(
			modelPath,
			result.prepared.prompt,
			sources,
			inferenceSettings,
			sourceSettings,
			onChunk);
	} else {
		result.inference = m_inference.generate(
			modelPath,
			result.prepared.prompt,
			inferenceSettings,
			onChunk);
	}

	result.structured = parseStructuredResult(result.inference.text);
	if (result.structured.unifiedDiff.empty() &&
		request.requestUnifiedDiff &&
		!result.structured.patchOperations.empty()) {
		result.structured.unifiedDiff =
			buildUnifiedDiffFromStructuredResult(result.structured);
	}
	return result;
}

ofxGgmlCodeAssistantInlineCompletionPreparedPrompt
ofxGgmlCodeAssistant::prepareInlineCompletion(
	const ofxGgmlCodeAssistantInlineCompletionRequest & request) const {
	ofxGgmlCodeAssistantInlineCompletionPreparedPrompt prepared;
	prepared.label = trimCopy(request.filePath);
	if (prepared.label.empty()) {
		prepared.label = "Inline completion";
	}

	std::ostringstream prompt;
	if (!trimCopy(request.language.systemPrompt).empty()) {
		prompt << request.language.systemPrompt << "\n";
	}
	prompt << "You are completing code at the current cursor position.\n";
	prompt << "Return only the missing code that should be inserted at the cursor.\n";
	if (request.useFillInTheMiddle) {
		prompt << "Use fill-in-the-middle reasoning and preserve surrounding syntax.\n";
	}
	if (request.singleLine) {
		prompt << "Keep the completion to a single line.\n";
	}
	if (!trimCopy(request.filePath).empty()) {
		prompt << "File: " << request.filePath << "\n";
	}
	if (!trimCopy(request.instruction).empty()) {
		prompt << "Instruction: " << request.instruction << "\n";
	}
	if (request.useFillInTheMiddle) {
		prompt << "\n<PRE>\n" << request.prefix << "\n</PRE>\n";
		prompt << "<SUF>\n" << request.suffix << "\n</SUF>\n\n";
	} else {
		prompt << "\nBefore cursor:\n";
		prompt << request.prefix << "\n";
		prompt << "<CURSOR>\n";
		prompt << "After cursor:\n";
		prompt << request.suffix << "\n\n";
	}
	prompt << "Completion:\n";
	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlCodeAssistantInlineCompletionResult
ofxGgmlCodeAssistant::runInlineCompletion(
	const std::string & modelPath,
	const ofxGgmlCodeAssistantInlineCompletionRequest & request,
	const ofxGgmlInferenceSettings & inferenceSettings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlCodeAssistantInlineCompletionResult result;
	result.prepared = prepareInlineCompletion(request);
	ofxGgmlInferenceSettings effectiveSettings = inferenceSettings;
	if (effectiveSettings.maxTokens <= 0 || effectiveSettings.maxTokens > request.maxTokens) {
		effectiveSettings.maxTokens = request.maxTokens;
	}
	result.inference = m_inference.generate(
		modelPath,
		result.prepared.prompt,
		effectiveSettings,
		onChunk);
	result.completion = trimCopy(result.inference.text);
	return result;
}
