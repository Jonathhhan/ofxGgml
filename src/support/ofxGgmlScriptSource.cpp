#include "ofxGgmlScriptSource.h"
#include "core/ofxGgmlWindowsUtf8.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr size_t kMaxCachedContentBytes = 16 * 1024 * 1024;
constexpr size_t kMaxCachedEntryBytes = 2 * 1024 * 1024;
constexpr auto kLocalMonitorPollInterval = std::chrono::milliseconds(1200);

void hashCombine(uint64_t * seed, uint64_t value) {
	if (seed == nullptr) {
		return;
	}
	*seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6) + (*seed >> 2);
}

static size_t totalCachedBytes(const std::vector<ofxGgmlScriptSourceFileEntry> & files) {
	size_t total = 0;
	for (const auto & f : files) {
		if (!f.isCached) continue;
		total += f.cachedContent.size();
	}
	return total;
}

std::string normalizeLower(const std::string & s) {
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return out;
}

std::vector<std::string> splitNonEmpty(
	const std::string & input,
	char delimiter) {
	std::vector<std::string> parts;
	std::string current;
	for (char c : input) {
		if (c == delimiter) {
			if (!current.empty()) {
				parts.push_back(current);
				current.clear();
			}
			continue;
		}
		current.push_back(c);
	}
	if (!current.empty()) {
		parts.push_back(current);
	}
	return parts;
}

bool isWorkspaceMetadataPath(const std::filesystem::path & path) {
	const std::string filename = normalizeLower(path.filename().string());
	const std::string ext = normalizeLower(path.extension().string());
	if (filename == "compile_commands.json" ||
		filename == "cmakelists.txt" ||
		filename == "addons.make" ||
		filename == "packages.config") {
		return true;
	}
	return ext == ".sln" ||
		ext == ".vcxproj" ||
		ext == ".filters" ||
		ext == ".props" ||
		ext == ".targets" ||
		ext == ".bat" ||
		ext == ".cmd" ||
		ext == ".rc";
}

uint64_t currentTimestampMs() {
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
}

std::string normalizeRelativePath(
	const std::filesystem::path & path,
	const std::filesystem::path & basePath) {
	std::error_code ec;
	const auto relative = std::filesystem::relative(path, basePath, ec);
	if (!ec && !relative.empty()) {
		return relative.generic_string();
	}
	return path.lexically_normal().generic_string();
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

std::string trimCopy(const std::string & text) {
	size_t start = 0;
	while (start < text.size() &&
		std::isspace(static_cast<unsigned char>(text[start]))) {
		++start;
	}
	size_t end = text.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}
	return text.substr(start, end - start);
}

std::vector<std::string> splitLines(const std::string & text) {
	std::vector<std::string> lines;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		lines.push_back(line);
	}
	return lines;
}

std::string getEnvVarString(const char * name) {
	if (name == nullptr || *name == '\0') {
		return {};
	}
#ifdef _WIN32
	char * value = nullptr;
	size_t len = 0;
	const errno_t err = _dupenv_s(&value, &len, name);
	if (err != 0 || value == nullptr || len == 0) {
		if (value != nullptr) {
			free(value);
		}
		return {};
	}
	std::string result(value);
	free(value);
	return result;
#else
	const char * value = std::getenv(name);
	return value ? std::string(value) : std::string();
#endif
}

bool pathExists(const std::filesystem::path & path) {
	std::error_code ec;
	return std::filesystem::exists(path, ec) && !ec;
}

#ifdef _WIN32
std::string quoteWindowsArg(const std::string & arg) {
	if (arg.empty()) {
		return "\"\"";
	}
	const bool needsQuotes =
		arg.find_first_of(" \t\"") != std::string::npos;
	if (!needsQuotes) {
		return arg;
	}

	std::string quoted = "\"";
	size_t backslashes = 0;
	for (char c : arg) {
		if (c == '\\') {
			++backslashes;
			continue;
		}
		if (c == '"') {
			quoted.append(backslashes * 2 + 1, '\\');
			quoted.push_back('"');
			backslashes = 0;
			continue;
		}
		if (backslashes > 0) {
			quoted.append(backslashes, '\\');
			backslashes = 0;
		}
		quoted.push_back(c);
	}
	if (backslashes > 0) {
		quoted.append(backslashes * 2, '\\');
	}
	quoted.push_back('"');
	return quoted;
}

std::string runExecutableAndCaptureLine(
	const std::string & executable,
	const std::vector<std::string> & arguments) {
	if (trimCopy(executable).empty()) {
		return {};
	}

	std::string output;
	SECURITY_ATTRIBUTES sa {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
		return {};
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW si {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = writePipe;
	si.hStdError = writePipe;

	std::string commandLine = quoteWindowsArg(executable);
	for (const auto & arg : arguments) {
		commandLine.push_back(' ');
		commandLine += quoteWindowsArg(arg);
	}

	std::wstring wideCommandLine = ofxGgmlWideFromUtf8(commandLine);
	std::vector<wchar_t> mutableCommandLine(wideCommandLine.begin(), wideCommandLine.end());
	mutableCommandLine.push_back(L'\0');

	PROCESS_INFORMATION pi {};
	const BOOL created = CreateProcessW(
		nullptr,
		mutableCommandLine.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&si,
		&pi);
	CloseHandle(writePipe);
	if (!created) {
		CloseHandle(readPipe);
		return {};
	}

	char buffer[512];
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) &&
		bytesRead > 0) {
		buffer[bytesRead] = '\0';
		output += buffer;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	CloseHandle(readPipe);

	for (const auto & line : splitLines(output)) {
		const std::string trimmed = trimCopy(line);
		if (!trimmed.empty()) {
			return trimmed;
		}
	}
	return {};
}

std::string resolveMsbuildPathViaVswhere() {
	const std::string installerDir = getEnvVarString("ProgramFiles(x86)");
	if (installerDir.empty()) {
		return {};
	}

	const std::filesystem::path vswherePath =
		std::filesystem::path(installerDir) /
		"Microsoft Visual Studio" / "Installer" / "vswhere.exe";
	if (!pathExists(vswherePath)) {
		return {};
	}

	return runExecutableAndCaptureLine(
		vswherePath.string(),
		{
			"-latest",
			"-products",
			"*",
			"-requires",
			"Microsoft.Component.MSBuild",
			"-find",
			"MSBuild\\**\\Bin\\MSBuild.exe"
		});
}

std::string fallbackMsbuildPath() {
	const std::vector<std::filesystem::path> candidates = {
		std::filesystem::path(getEnvVarString("ProgramFiles")) /
			"Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin/MSBuild.exe",
		std::filesystem::path(getEnvVarString("ProgramFiles")) /
			"Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe",
		std::filesystem::path(getEnvVarString("ProgramFiles(x86)")) /
			"Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe",
		std::filesystem::path(getEnvVarString("ProgramFiles(x86)")) /
			"Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe"
	};
	for (const auto & candidate : candidates) {
		if (pathExists(candidate)) {
			return candidate.string();
		}
	}
	return "MSBuild.exe";
}
#endif

struct VisualStudioParseResult {
	std::vector<ofxGgmlScriptSourceVisualStudioProjectInfo> projects;
	std::vector<std::string> configurations;
	std::vector<std::string> platforms;
};

VisualStudioParseResult parseVisualStudioSolution(
	const std::filesystem::path & solutionPath,
	const std::filesystem::path & rootPath) {
	VisualStudioParseResult result;
	const std::string text = readTextFile(solutionPath);
	if (text.empty()) {
		return result;
	}

	static const std::regex projectPattern(
		R"(^Project\(\"[^\"]+\"\)\s*=\s*\"([^\"]+)\",\s*\"([^\"]+)\",\s*\"([^\"]+)\"\s*$)");
	static const std::regex configPattern(
		R"(^\s*([^\s=]+)\s*=\s*([^\s=]+)\s*$)");
	bool insideConfigSection = false;
	std::set<std::string> configs;
	std::set<std::string> platforms;

	for (const auto & rawLine : splitLines(text)) {
		const std::string line = trimCopy(rawLine);
		if (line.empty()) {
			continue;
		}

		std::smatch projectMatch;
		if (std::regex_match(line, projectMatch, projectPattern) &&
			projectMatch.size() >= 4) {
			const std::string projectPath =
				std::filesystem::path(projectMatch[2].str()).generic_string();
			if (normalizeLower(std::filesystem::path(projectPath).extension().string()) ==
				".vcxproj") {
				ofxGgmlScriptSourceVisualStudioProjectInfo projectInfo;
				projectInfo.name = projectMatch[1].str();
				projectInfo.relativePath = projectPath;
				projectInfo.projectGuid = projectMatch[3].str();
				result.projects.push_back(std::move(projectInfo));
			}
			continue;
		}

		if (line.find("GlobalSection(SolutionConfigurationPlatforms)") !=
			std::string::npos) {
			insideConfigSection = true;
			continue;
		}
		if (insideConfigSection && line.find("EndGlobalSection") != std::string::npos) {
			insideConfigSection = false;
			continue;
		}
		if (!insideConfigSection) {
			continue;
		}

		std::smatch configMatch;
		if (!std::regex_match(line, configMatch, configPattern) ||
			configMatch.size() < 2) {
			continue;
		}
		const std::string configPlatform = configMatch[1].str();
		const size_t sep = configPlatform.find('|');
		if (sep == std::string::npos) {
			continue;
		}
		configs.insert(configPlatform.substr(0, sep));
		platforms.insert(configPlatform.substr(sep + 1));
	}

	result.configurations.assign(configs.begin(), configs.end());
	result.platforms.assign(platforms.begin(), platforms.end());
	std::sort(result.projects.begin(), result.projects.end(),
		[](const ofxGgmlScriptSourceVisualStudioProjectInfo & a,
			const ofxGgmlScriptSourceVisualStudioProjectInfo & b) {
			return a.relativePath < b.relativePath;
		});
	return result;
}

ofHttpResponse performHttpRequest(
	const std::string & url,
	const std::string & authToken,
	const std::map<std::string, std::string> & extraHeaders = {}) {
#ifdef OFXGGML_HEADLESS_STUBS
	(void) authToken;
	(void) extraHeaders;
	return ofLoadURL(url);
#else
	ofHttpRequest request(url, url, false, true, false);
	request.method = ofHttpRequest::GET;
	request.headers["Accept"] = "application/vnd.github+json";
	request.headers["User-Agent"] = "ofxGgml/1.0.0";
	for (const auto & pair : extraHeaders) {
		request.headers[pair.first] = pair.second;
	}
	if (!authToken.empty()) {
		request.headers["Authorization"] = "Bearer " + authToken;
	}
	ofURLFileLoader loader;
	return loader.handleRequest(request);
#endif
}

std::string parseGitHubMessage(const std::string & bodyText) {
	const ofJson json = ofJson::parse(bodyText, nullptr, false);
	if (!json.is_discarded() && json.is_object()) {
		if (json.contains("message")) {
			return json.value("message", "");
		}
	}
	return {};
}

std::string buildGitHubDiagnostic(
	int status,
	const std::string & bodyText,
	bool hasToken) {
	const std::string apiMessage = parseGitHubMessage(bodyText);
	if (status == 401 || status == 403) {
		const bool looksLikeRateLimit =
			normalizeLower(apiMessage).find("rate limit") != std::string::npos;
		if (looksLikeRateLimit) {
			return hasToken
				? "GitHub API rate limit reached. Wait or use a higher-limit token."
				: "GitHub API rate limit reached. Set GITHUB_TOKEN or GH_TOKEN for higher limits.";
		}
		return hasToken
			? "GitHub API request was rejected. Check the configured token permissions."
			: "GitHub API request was rejected. Configure GITHUB_TOKEN or GH_TOKEN if the repo needs authentication.";
	}
	if (status == 404) {
		return "GitHub repository or branch was not found.";
	}
	if (status <= 0) {
		return "GitHub request failed before receiving a response.";
	}
	if (!apiMessage.empty()) {
		return "GitHub API error: " + apiMessage;
	}
	return "GitHub API error: HTTP " + ofToString(status);
}

std::string chooseDefaultBuildDirectory(
	const std::filesystem::path & rootPath,
	const std::string & compilationDatabasePath) {
	if (!compilationDatabasePath.empty()) {
		const std::filesystem::path dbPath = rootPath / compilationDatabasePath;
		if (pathExists(dbPath.parent_path())) {
			return normalizeRelativePath(dbPath.parent_path(), rootPath);
		}
	}

	const std::vector<std::filesystem::path> candidates = {
		rootPath / "build",
		rootPath / "out/build",
		rootPath / "tests/build"
	};
	for (const auto & candidate : candidates) {
		if (pathExists(candidate)) {
			return normalizeRelativePath(candidate, rootPath);
		}
	}
	return {};
}

}

ofxGgmlScriptSource::~ofxGgmlScriptSource() {
	cancelFetchWorker("Fetch canceled: source destroyed");
	stopLocalMonitor();
}

void ofxGgmlScriptSource::clear() {
	cancelFetchWorker("Fetch canceled: source cleared");
	stopLocalMonitor();
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::None;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
	m_internetUrls.clear();
	m_files.clear();
	m_status.clear();
	clearWorkspaceInfoLocked();
	m_fetchDiagnostics.clear();
}

void ofxGgmlScriptSource::setGitHubMode() {
	cancelFetchWorker("Fetch canceled: switched to GitHub mode");
	stopLocalMonitor();
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
	m_localFolderPath.clear();
	m_internetUrls.clear();
	m_files.clear();
	m_status.clear();
	clearWorkspaceInfoLocked();
}

void ofxGgmlScriptSource::setInternetMode() {
	cancelFetchWorker("Fetch canceled: switched to Internet mode");
	stopLocalMonitor();
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_sourceType != ofxGgmlScriptSourceType::Internet) {
		m_internetUrls.clear();
		m_files.clear();
		m_status.clear();
	}
	m_sourceType = ofxGgmlScriptSourceType::Internet;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
	clearWorkspaceInfoLocked();
}

void ofxGgmlScriptSource::setPreferredExtension(const std::string & ext) {
	std::string normalized = trim(ext);
	if (!normalized.empty() && normalized.front() != '.') {
		normalized = "." + normalized;
	}
	normalized = normalizeLower(normalized);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_preferredExtension = normalized;
	}
	rescan();
}

std::string ofxGgmlScriptSource::getPreferredExtension() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_preferredExtension;
}

bool ofxGgmlScriptSource::setLocalFolder(const std::string & path) {
	cancelFetchWorker("Fetch canceled: switched to local folder");
	stopLocalMonitor();
	bool success = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::LocalFolder;
		m_localFolderPath = path;
		m_gitHubOwnerRepo.clear();
		m_gitHubBranch.clear();
		m_internetUrls.clear();
		success = scanLocalFolderLocked();
	}
	if (success) {
		startLocalMonitor();
	}
	return success;
}

bool ofxGgmlScriptSource::setVisualStudioWorkspace(const std::string & path) {
	std::error_code ec;
	std::filesystem::path workspacePath = std::filesystem::path(trim(path));
	if (workspacePath.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Visual Studio path is empty";
		return false;
	}

	workspacePath = std::filesystem::weakly_canonical(workspacePath, ec);
	if (ec || workspacePath.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Visual Studio path is invalid";
		return false;
	}

	const bool isDirectory = std::filesystem::is_directory(workspacePath, ec) && !ec;
	const std::filesystem::path root = isDirectory
		? workspacePath
		: workspacePath.parent_path();
	if (root.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Visual Studio workspace root is invalid";
		return false;
	}

	if (!setLocalFolder(root.string())) {
		return false;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_workspaceInfo.activeVisualStudioPath =
		normalizeRelativePath(workspacePath, root);
	if (!isDirectory) {
		const std::string ext = normalizeLower(workspacePath.extension().string());
		if (ext == ".sln") {
			m_workspaceInfo.hasVisualStudioSolution = true;
			m_workspaceInfo.visualStudioSolutionPath =
				normalizeRelativePath(workspacePath, root);
		} else if (ext == ".vcxproj") {
			const std::string normalizedProject =
				normalizeRelativePath(workspacePath, root);
			if (std::find(
					m_workspaceInfo.visualStudioProjectPaths.begin(),
					m_workspaceInfo.visualStudioProjectPaths.end(),
					normalizedProject) ==
				m_workspaceInfo.visualStudioProjectPaths.end()) {
				m_workspaceInfo.visualStudioProjectPaths.insert(
						m_workspaceInfo.visualStudioProjectPaths.begin(),
						normalizedProject);
			}
			m_workspaceInfo.selectedVisualStudioProjectPath = normalizedProject;
			m_workspaceInfo.hasExplicitVisualStudioProjectSelection = true;
		}
	}
	if (m_workspaceInfo.hasVisualStudioSolution ||
		!m_workspaceInfo.visualStudioProjectPaths.empty()) {
		m_status = "Loaded Visual Studio workspace";
	}
	return true;
}

bool ofxGgmlScriptSource::configureVisualStudioWorkspace(
	const std::string & projectPath,
	const std::string & configuration,
	const std::string & platform) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_sourceType != ofxGgmlScriptSourceType::LocalFolder) {
		return false;
	}

	if (!trim(projectPath).empty()) {
		m_workspaceInfo.selectedVisualStudioProjectPath =
			std::filesystem::path(projectPath).generic_string();
		m_workspaceInfo.hasExplicitVisualStudioProjectSelection = true;
	}
	if (!trim(configuration).empty()) {
		m_workspaceInfo.selectedVisualStudioConfiguration = trim(configuration);
	}
	if (!trim(platform).empty()) {
		m_workspaceInfo.selectedVisualStudioPlatform = trim(platform);
	}
	m_workspaceInfo.lastScanTimestampMs = currentTimestampMs();
	++m_workspaceInfo.workspaceGeneration;
	return true;
}

bool ofxGgmlScriptSource::setGitHubRepo(const std::string & ownerRepo, const std::string & branch) {
	const std::string ownerRepoTrim = trim(ownerRepo);
	const std::string branchTrim = sanitizeGitHubBranch(branch);
	if (!isValidOwnerRepo(ownerRepoTrim)) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
		m_localFolderPath.clear();
		m_gitHubOwnerRepo = ownerRepoTrim;
		m_gitHubBranch = branchTrim;
		m_files.clear();
		m_status = "Invalid repo format (use owner/repo)";
		return false;
	}
	if (!branchTrim.empty() && !isValidBranch(branchTrim)) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
		m_localFolderPath.clear();
		m_gitHubOwnerRepo = ownerRepoTrim;
		m_gitHubBranch = branchTrim;
		m_files.clear();
		m_status = "Invalid branch name";
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
		m_localFolderPath.clear();
		m_gitHubOwnerRepo = ownerRepoTrim;
		m_gitHubBranch = branchTrim;
		m_internetUrls.clear();
		m_files.clear();
		m_status.clear();
		clearWorkspaceInfoLocked();
	}

	return true;
}

bool ofxGgmlScriptSource::setGitHubRepoFromInput(
	const std::string & ownerRepoOrUrl,
	const std::string & branchHint) {
	std::string ownerRepo;
	std::string branch;
	std::string focusedPath;
	if (!parseGitHubInput(
			ownerRepoOrUrl,
			branchHint,
			&ownerRepo,
			&branch,
			&focusedPath)) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
		m_gitHubOwnerRepo = trim(ownerRepoOrUrl);
		m_gitHubBranch = sanitizeGitHubBranch(branchHint);
		m_files.clear();
		m_status = "Invalid GitHub input (use owner/repo or a GitHub URL)";
		clearWorkspaceInfoLocked();
		return false;
	}
	if (!setGitHubRepo(ownerRepo, branch)) {
		return false;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_workspaceInfo.gitHubFocusedPath =
		std::filesystem::path(focusedPath).generic_string();
	return true;
}

void ofxGgmlScriptSource::setGitHubAuthToken(const std::string & token) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_gitHubAuthToken = trim(token);
}

void ofxGgmlScriptSource::setInternetUrls(const std::vector<std::string> & urls) {
	cancelFetchWorker("Fetch canceled: internet sources updated");
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::Internet;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
	m_internetUrls.clear();
	m_internetUrls.reserve(urls.size()); // Pre-allocate capacity
	for (const auto & url : urls) {
		std::string trimmed = trim(url);
		if (isValidUrl(trimmed)) {
			m_internetUrls.push_back(std::move(trimmed)); // Use move semantics
		}
	}

	m_files.clear();
	clearWorkspaceInfoLocked();
	m_files.reserve(m_internetUrls.size()); // Pre-allocate capacity
	for (const auto & url : m_internetUrls) {
		ofxGgmlScriptSourceFileEntry fe;
		fe.name = url.size() > 96 ? url.substr(0, 96) + "..." : url;
		fe.fullPath = url;
		fe.isDirectory = false;
		m_files.push_back(std::move(fe));
	}
	m_status = m_internetUrls.empty()
		? "No internet sources"
		: ofToString(m_internetUrls.size()) + " internet sources";
}

bool ofxGgmlScriptSource::addInternetUrl(const std::string & url) {
	std::string trimmed = trim(url);
	if (!isValidUrl(trimmed)) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::Internet;
		m_status = "Invalid URL (use http/https)";
		return false;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::Internet;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
	clearWorkspaceInfoLocked();

	auto it = std::find(m_internetUrls.begin(), m_internetUrls.end(), trimmed);
	if (it == m_internetUrls.end()) {
		m_internetUrls.push_back(std::move(trimmed)); // Use move semantics
	}

	// Rebuild file list to reflect latest URLs.
	m_files.clear();
	m_files.reserve(m_internetUrls.size()); // Pre-allocate capacity
	for (const auto & entryUrl : m_internetUrls) {
		ofxGgmlScriptSourceFileEntry fe;
		fe.name = entryUrl.size() > 96 ? entryUrl.substr(0, 96) + "..." : entryUrl;
		fe.fullPath = entryUrl;
		fe.isDirectory = false;
		m_files.push_back(std::move(fe));
	}

	m_status = m_internetUrls.empty()
		? "No internet sources"
		: ofToString(m_internetUrls.size()) + " internet sources";
	return true;
}

bool ofxGgmlScriptSource::removeInternetUrl(size_t index) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (index >= m_internetUrls.size()) return false;
	m_internetUrls.erase(m_internetUrls.begin() + static_cast<long>(index));

	m_files.clear();
	for (const auto & entryUrl : m_internetUrls) {
		ofxGgmlScriptSourceFileEntry fe;
		fe.name = entryUrl.size() > 96 ? entryUrl.substr(0, 96) + "..." : entryUrl;
		fe.fullPath = entryUrl;
		fe.isDirectory = false;
		m_files.push_back(std::move(fe));
	}

	m_status = m_internetUrls.empty()
		? "No internet sources"
		: ofToString(m_internetUrls.size()) + " internet sources";
	return true;
}

bool ofxGgmlScriptSource::fetchGitHubRepo() {
	cancelFetchWorker("Fetch canceled: superseded by new fetch");

	std::string ownerRepo;
	std::string branch;
	std::string preferredExt;
	std::string focusedPath;
	std::string authToken;
	ofxGgmlScriptSourceType sourceType;
	uint64_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		sourceType = m_sourceType;
		if (sourceType != ofxGgmlScriptSourceType::GitHubRepo) {
			m_status = "Source is not GitHub";
			return false;
		}
		ownerRepo = m_gitHubOwnerRepo;
		branch = m_gitHubBranch;
		preferredExt = m_preferredExtension;
		focusedPath = m_workspaceInfo.gitHubFocusedPath;
		authToken = m_gitHubAuthToken.empty()
			? getEnvVarString("GITHUB_TOKEN")
			: m_gitHubAuthToken;
		if (authToken.empty()) {
			authToken = getEnvVarString("GH_TOKEN");
		}
		if (!isValidOwnerRepo(ownerRepo)) {
			m_status = "Invalid repo format (use owner/repo)";
			return false;
		}
		if (!branch.empty() && !isValidBranch(branch)) {
			m_status = "Invalid branch name";
			return false;
		}
		m_files.clear();
		m_status = "Fetching...";
		m_cancelFetch.store(false, std::memory_order_release);
		generation = m_fetchGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
		pushFetchDiagnosticLocked("start", "GitHub fetch started", generation);
	}
	m_fetching.store(true, std::memory_order_release);

	std::thread worker([this,
			generation,
			ownerRepo,
			branch,
			preferredExt,
			focusedPath,
			authToken,
			sourceType]() {
		std::vector<ofxGgmlScriptSourceFileEntry> entries;
		std::string status;
		std::string resolvedBranch = branch;
		std::string defaultBranch;
		std::string resolvedCommitSha;
		std::string diagnostic;
		bool hadError = false;

		const auto repoResponse = performHttpRequest(
			"https://api.github.com/repos/" + ownerRepo,
			authToken);
		if (repoResponse.status >= 200 && repoResponse.status < 300) {
			const ofJson repoJson =
				ofJson::parse(repoResponse.data.getText(), nullptr, false);
			if (!repoJson.is_discarded()) {
				defaultBranch = repoJson.value("default_branch", "");
			}
		} else if (repoResponse.status != 0) {
			diagnostic = buildGitHubDiagnostic(
				repoResponse.status,
				repoResponse.data.getText(),
				!authToken.empty());
		}

		if (resolvedBranch.empty()) {
			resolvedBranch = defaultBranch;
		}
		if (resolvedBranch.empty()) {
			status = diagnostic.empty()
				? "Failed to resolve default GitHub branch"
				: diagnostic;
			hadError = true;
		}

		if (!hadError) {
			const auto branchResponse = performHttpRequest(
				"https://api.github.com/repos/" + ownerRepo + "/branches/" + resolvedBranch,
				authToken);
			if (branchResponse.status >= 200 && branchResponse.status < 300) {
				const ofJson branchJson =
					ofJson::parse(branchResponse.data.getText(), nullptr, false);
				if (!branchJson.is_discarded() &&
					branchJson.contains("commit") &&
					branchJson["commit"].is_object()) {
					resolvedCommitSha =
						branchJson["commit"].value("sha", "");
				}
			} else {
				diagnostic = buildGitHubDiagnostic(
					branchResponse.status,
					branchResponse.data.getText(),
					!authToken.empty());
			}
		}

		if (!hadError && resolvedCommitSha.empty()) {
			status = diagnostic.empty()
				? "Failed to resolve GitHub branch commit"
				: diagnostic;
			hadError = true;
		}

		const std::string cacheKey =
			ownerRepo + "|" + resolvedBranch + "|" + resolvedCommitSha + "|" +
			std::filesystem::path(focusedPath).generic_string() + "|" + preferredExt;
		if (!hadError) {
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto cacheIt = m_gitHubTreeCache.find(cacheKey);
			if (cacheIt != m_gitHubTreeCache.end()) {
				entries = cacheIt->second.entries;
				status = ofToString(entries.size()) +
					" files from GitHub (" + resolvedBranch + " @ " +
					resolvedCommitSha.substr(0, (std::min<size_t>)(resolvedCommitSha.size(), 12)) +
					", cached)";
			}
		}

		if (m_cancelFetch.load(std::memory_order_acquire) ||
			m_fetchGeneration.load(std::memory_order_acquire) != generation) {
			m_fetching.store(false, std::memory_order_release);
			return;
		}

		if (!hadError && entries.empty()) {
			const auto treeResponse = performHttpRequest(
				"https://api.github.com/repos/" + ownerRepo +
					"/git/trees/" + resolvedCommitSha + "?recursive=1",
				authToken);
			if (treeResponse.status < 200 || treeResponse.status >= 300) {
				status = buildGitHubDiagnostic(
					treeResponse.status,
					treeResponse.data.getText(),
					!authToken.empty());
				hadError = true;
			} else {
				const ofJson json =
					ofJson::parse(treeResponse.data.getText(), nullptr, false);
				if (json.is_discarded() || !json.contains("tree") ||
					!json["tree"].is_array()) {
					status = "Failed to parse GitHub tree response";
					hadError = true;
				} else {
					const std::string normalizedFocusedPath =
						std::filesystem::path(focusedPath).generic_string();
					const std::string rawPrefix =
						"https://raw.githubusercontent.com/" +
						ownerRepo + "/" + resolvedCommitSha + "/";
					for (const auto & item : json["tree"]) {
						if (m_cancelFetch.load(std::memory_order_acquire) ||
							m_fetchGeneration.load(std::memory_order_acquire) != generation) {
							m_fetching.store(false, std::memory_order_release);
							return;
						}
						if (!item.is_object() || item.value("type", "") != "blob") {
							continue;
						}

						const std::string path = item.value("path", "");
						if (!isSafeRepoPath(path)) {
							continue;
						}
						if (!normalizedFocusedPath.empty()) {
							const bool exactMatch = path == normalizedFocusedPath;
							const bool prefixMatch =
								path.rfind(normalizedFocusedPath + "/", 0) == 0;
							if (!exactMatch && !prefixMatch) {
								continue;
							}
						}

						const std::filesystem::path filePath(path);
						const std::string ext =
							normalizeLower(filePath.extension().string());
						bool include =
							hasSourceExtension(ext, true) ||
							isWorkspaceMetadataPath(filePath);
						if (!preferredExt.empty() && !isWorkspaceMetadataPath(filePath)) {
							include = (ext == preferredExt);
						}
						if (!include) {
							continue;
						}

						ofxGgmlScriptSourceFileEntry entry;
						entry.name = path;
						entry.fullPath = rawPrefix + path;
						entry.isDirectory = false;
						entries.push_back(std::move(entry));
					}

					std::sort(entries.begin(), entries.end(),
						[](const ofxGgmlScriptSourceFileEntry & a,
							const ofxGgmlScriptSourceFileEntry & b) {
							return a.name < b.name;
						});
					status = ofToString(entries.size()) + " files from GitHub (" +
						resolvedBranch + " @ " +
						resolvedCommitSha.substr(0, (std::min<size_t>)(resolvedCommitSha.size(), 12)) +
						")";
				}
			}
		}

		if (!hadError && !entries.empty()) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_gitHubTreeCache[cacheKey] = GitHubTreeCacheEntry{
				ownerRepo,
				resolvedBranch,
				resolvedCommitSha,
				std::filesystem::path(focusedPath).generic_string(),
				preferredExt,
				entries,
				currentTimestampMs()
			};
		}

		if (m_cancelFetch.load(std::memory_order_acquire) || m_fetchGeneration.load(std::memory_order_acquire) != generation) {
			m_fetching.store(false, std::memory_order_release);
			return;
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_sourceType == sourceType &&
				m_fetchGeneration.load(std::memory_order_acquire) == generation) {
				m_files = std::move(entries);
				m_gitHubBranch = resolvedBranch;
				m_workspaceInfo.workspaceRoot = ownerRepo;
				m_workspaceInfo.workspaceLabel = ownerRepo;
				m_workspaceInfo.gitHubDefaultBranch = defaultBranch;
				m_workspaceInfo.gitHubResolvedCommitSha = resolvedCommitSha;
				m_workspaceInfo.gitHubFocusedPath =
					std::filesystem::path(focusedPath).generic_string();
				m_workspaceInfo.gitHubDiagnostic = diagnostic;
				m_workspaceInfo.gitHubTreeCached =
					status.find("cached") != std::string::npos;
				m_workspaceInfo.lastScanTimestampMs = currentTimestampMs();
				++m_workspaceInfo.workspaceGeneration;
				m_status = status;
				pushFetchDiagnosticLocked(
					hadError ? "error" : "complete",
					status,
					generation);
			}
		}
		m_fetching.store(false, std::memory_order_release);
	});
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_fetchThread = std::move(worker);
	}

	return true;
}

bool ofxGgmlScriptSource::rescan() {
	ofxGgmlScriptSourceType type;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		type = m_sourceType;
	}
	if (type == ofxGgmlScriptSourceType::LocalFolder) {
		std::lock_guard<std::mutex> lock(m_mutex);
		return scanLocalFolderLocked();
	} else if (type == ofxGgmlScriptSourceType::Internet) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_files.clear();
		for (const auto & url : m_internetUrls) {
			ofxGgmlScriptSourceFileEntry fe;
			fe.name = url.size() > 96 ? url.substr(0, 96) + "..." : url;
			fe.fullPath = url;
			fe.isDirectory = false;
			m_files.push_back(std::move(fe));
		}
		m_status = m_internetUrls.empty()
			? "No internet sources"
			: ofToString(m_internetUrls.size()) + " internet sources";
		return true;
	}
	return true;
}

bool ofxGgmlScriptSource::loadFileContent(int index, std::string & outContent) {
	outContent.clear();
	ofxGgmlScriptSourceType type = ofxGgmlScriptSourceType::None;
	ofxGgmlScriptSourceFileEntry entry;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (index < 0 || index >= static_cast<int>(m_files.size())) return false;
		entry = m_files[static_cast<size_t>(index)];
		type = m_sourceType;
	}
	if (entry.isDirectory) return false;

	if (entry.isCached) {
		outContent = entry.cachedContent;
		return true;
	}

	if (type == ofxGgmlScriptSourceType::LocalFolder) {
		std::ifstream in(entry.fullPath, std::ios::binary);
		if (!in.is_open()) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Failed to load: " + entry.name;
			return false;
		}
		std::string content((std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
		in.close();
		outContent = std::move(content);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_files[static_cast<size_t>(index)].cachedContent = outContent;
		m_files[static_cast<size_t>(index)].isCached = true;
		m_status = "Loaded: " + entry.name;
		return true;
	}

	if (type == ofxGgmlScriptSourceType::GitHubRepo) {
		const std::string expectedPrefix = "https://raw.githubusercontent.com/";
		if (entry.fullPath.rfind(expectedPrefix, 0) != 0) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Invalid URL: " + entry.name;
			return false;
		}
		ofHttpResponse response = ofLoadURL(entry.fullPath);
		if (response.status < 200 || response.status >= 300) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Failed to download: " + entry.name;
			return false;
		}
		outContent = response.data.getText();
		std::lock_guard<std::mutex> lock(m_mutex);
		m_files[static_cast<size_t>(index)].cachedContent = outContent;
		m_files[static_cast<size_t>(index)].isCached = true;
		m_status = "Loaded: " + entry.name;
		return true;
	}

	if (type == ofxGgmlScriptSourceType::Internet) {
		if (!isValidUrl(entry.fullPath)) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Invalid URL: " + entry.name;
			return false;
		}
		ofHttpResponse response = ofLoadURL(entry.fullPath);
		if (response.status < 200 || response.status >= 300) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Failed to download: " + entry.name;
			return false;
		}
		outContent = response.data.getText();
		std::lock_guard<std::mutex> lock(m_mutex);
		m_files[static_cast<size_t>(index)].cachedContent = outContent;
		m_files[static_cast<size_t>(index)].isCached = true;
		m_status = "Loaded: " + entry.name;
		return true;
	}

	return false;
}

bool ofxGgmlScriptSource::saveToLocalSource(const std::string & filename, const std::string & content) {
	std::string folder;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_sourceType != ofxGgmlScriptSourceType::LocalFolder || m_localFolderPath.empty()) {
			return false;
		}
		folder = m_localFolderPath;
	}

	if (filename.empty() || filename.find('/') != std::string::npos ||
		filename.find('\\') != std::string::npos || filename.find("..") != std::string::npos) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Invalid filename";
		return false;
	}

	const std::filesystem::path outPath = std::filesystem::path(folder) / filename;
	std::ofstream out(outPath, std::ios::binary);
	if (!out.is_open()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Failed to save: " + filename;
		return false;
	}
	out << content;
	out.close();

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Saved: " + filename;
	}
	return rescan();
}

ofxGgmlScriptSourceType ofxGgmlScriptSource::getSourceType() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_sourceType;
}

std::string ofxGgmlScriptSource::getLocalFolderPath() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_localFolderPath;
}

std::string ofxGgmlScriptSource::getGitHubOwnerRepo() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gitHubOwnerRepo;
}

std::string ofxGgmlScriptSource::getGitHubBranch() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gitHubBranch;
}

std::vector<std::string> ofxGgmlScriptSource::getInternetUrls() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_internetUrls;
}

std::vector<ofxGgmlScriptSourceFileEntry> ofxGgmlScriptSource::getFiles() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_files;
}

ofxGgmlScriptSourceWorkspaceInfo ofxGgmlScriptSource::getWorkspaceInfo() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_workspaceInfo;
}

std::string ofxGgmlScriptSource::getStatus() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_status;
}

bool ofxGgmlScriptSource::isFetching() const noexcept {
	return m_fetching.load(std::memory_order_acquire);
}

std::vector<ofxGgmlScriptSourceFetchDiagnostic> ofxGgmlScriptSource::getFetchDiagnostics() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_fetchDiagnostics;
}

void ofxGgmlScriptSource::cancelFetchWorker(const std::string & reason) {
	std::thread worker;
	bool wasFetching = false;
	bool hadWorker = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		wasFetching = m_fetching.load();
		hadWorker = m_fetchThread.joinable();
		if (!wasFetching && !hadWorker) {
			return;
		}
		const uint64_t generation = m_fetchGeneration.fetch_add(1) + 1;
		m_cancelFetch.store(true);
		m_fetching.store(false);
		if (wasFetching) {
			pushFetchDiagnosticLocked("cancel", reason, generation);
			if (m_sourceType == ofxGgmlScriptSourceType::GitHubRepo) {
				m_status = "Fetch canceled";
			}
		}
		worker = std::move(m_fetchThread);
	}
	if (worker.joinable()) {
		worker.join();
	}
}

void ofxGgmlScriptSource::clearWorkspaceInfoLocked() {
	m_workspaceInfo = {};
}

void ofxGgmlScriptSource::refreshWorkspaceInfoLocked(
	const std::string & requestedVisualStudioPath) {
	if (trim(m_localFolderPath).empty()) {
		clearWorkspaceInfoLocked();
		return;
	}
	m_workspaceInfo = buildWorkspaceInfoForFolder(
		m_localFolderPath,
		m_workspaceInfo,
		requestedVisualStudioPath);
	m_workspaceInfo.localBackgroundMonitoringEnabled =
		!m_stopLocalMonitor.load(std::memory_order_acquire);
}

void ofxGgmlScriptSource::startLocalMonitor() {
	stopLocalMonitor();

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_sourceType != ofxGgmlScriptSourceType::LocalFolder ||
			trim(m_localFolderPath).empty()) {
			return;
		}
		m_stopLocalMonitor.store(false, std::memory_order_release);
		m_workspaceInfo.localBackgroundMonitoringEnabled = true;
	}

	m_localMonitorThread = std::thread([this]() {
		uint64_t lastFingerprint = 0;
		bool fingerprintInitialized = false;

		for (;;) {
			std::string localFolderPath;
			std::string preferredExt;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_stopLocalMonitor.load(std::memory_order_acquire) ||
					m_sourceType != ofxGgmlScriptSourceType::LocalFolder ||
					trim(m_localFolderPath).empty()) {
					break;
				}
				localFolderPath = m_localFolderPath;
				preferredExt = m_preferredExtension;
			}

			const uint64_t fingerprint =
				computeLocalWorkspaceFingerprint(localFolderPath, preferredExt);
			if (!fingerprintInitialized) {
				lastFingerprint = fingerprint;
				fingerprintInitialized = true;
			} else if (fingerprint != 0 && fingerprint != lastFingerprint) {
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_stopLocalMonitor.load(std::memory_order_acquire) ||
					m_sourceType != ofxGgmlScriptSourceType::LocalFolder ||
					m_localFolderPath != localFolderPath) {
					break;
				}
				if (scanLocalFolderLocked()) {
					m_workspaceInfo.lastObservedChangeTimestampMs = currentTimestampMs();
					m_workspaceInfo.localBackgroundMonitoringEnabled = true;
					m_status = ofToString(m_files.size()) + " items (auto-refreshed)";
				}
				lastFingerprint = fingerprint;
			}

			std::this_thread::sleep_for(kLocalMonitorPollInterval);
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		m_workspaceInfo.localBackgroundMonitoringEnabled = false;
	});
}

void ofxGgmlScriptSource::stopLocalMonitor() {
	m_stopLocalMonitor.store(true, std::memory_order_release);
	if (m_localMonitorThread.joinable()) {
		m_localMonitorThread.join();
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	m_workspaceInfo.localBackgroundMonitoringEnabled = false;
}

/// Creates a fetch diagnostic entry with timestamp. Maintains maximum of 64 entries.
/// Must be called with m_mutex locked.
void ofxGgmlScriptSource::pushFetchDiagnosticLocked(
	const std::string & state,
	const std::string & message,
	uint64_t generation) {
	ofxGgmlScriptSourceFetchDiagnostic diagnostic;
	diagnostic.state = state;
	diagnostic.message = message;
	diagnostic.generation = generation;
	diagnostic.timestampMs = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
	m_fetchDiagnostics.push_back(std::move(diagnostic));
	static constexpr size_t kMaxDiagnostics = 64;
	if (m_fetchDiagnostics.size() > kMaxDiagnostics) {
		m_fetchDiagnostics.erase(m_fetchDiagnostics.begin(),
			m_fetchDiagnostics.begin() + (m_fetchDiagnostics.size() - kMaxDiagnostics));
	}
}

bool ofxGgmlScriptSource::scanLocalFolderLocked() {
	m_files.clear();
	std::error_code ec;
	if (!std::filesystem::is_directory(m_localFolderPath, ec) || ec) {
		m_status = "Not a directory";
		clearWorkspaceInfoLocked();
		return false;
	}
	m_files = scanLocalFolderEntries(m_localFolderPath);
	refreshWorkspaceInfoLocked(m_workspaceInfo.activeVisualStudioPath);
	m_status = ofToString(m_files.size()) + " items";
	return true;
}

std::vector<ofxGgmlScriptSourceFileEntry> ofxGgmlScriptSource::scanLocalFolderEntries(
	const std::string & path) const {
	std::vector<ofxGgmlScriptSourceFileEntry> files;
	std::error_code ec;
	const std::string preferredExt = m_preferredExtension;
	const std::filesystem::path basePath(path);

	auto it = std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec);
	auto end = std::filesystem::recursive_directory_iterator();
	for (; it != end; it.increment(ec)) {
		if (ec) {
			ec.clear();
			continue;
		}
		const auto& entry = *it;
		std::string filename = entry.path().filename().string();

		// Skip hidden folders like .git to significantly speed up indexing
		if (entry.is_directory(ec) && !filename.empty() && filename[0] == '.') {
			it.disable_recursion_pending();
			continue;
		}

		ofxGgmlScriptSourceFileEntry fe;
		// Use relative path from base directory for better organization
		ec.clear();
		const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), basePath, ec);
		if (ec) {
			fe.name = filename;
			ec.clear();
		} else {
			fe.name = relativePath.string();
		}
		fe.fullPath = entry.path().string();
		fe.isDirectory = entry.is_directory(ec);
		if (ec) {
			ec.clear();
			continue;
		}

		if (fe.isDirectory) {
			files.push_back(std::move(fe));
			continue;
		}

		const std::string ext = normalizeLower(entry.path().extension().string());
		const bool isWorkspaceMetadata = isWorkspaceMetadataPath(entry.path());
		bool include = hasSourceExtension(ext, false) || isWorkspaceMetadata;
		if (!preferredExt.empty() && !isWorkspaceMetadata) {
			include = (ext == preferredExt);
		}
		if (include) {
			files.push_back(std::move(fe));
		}
	}

	std::sort(files.begin(), files.end(),
		[](const ofxGgmlScriptSourceFileEntry & a, const ofxGgmlScriptSourceFileEntry & b) {
			if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
			return a.name < b.name;
		});
	return files;
}

uint64_t ofxGgmlScriptSource::computeLocalWorkspaceFingerprint(
	const std::string & path,
	const std::string & preferredExt) const {
	if (trim(path).empty()) {
		return 0;
	}

	uint64_t fingerprint = 1469598103934665603ULL;
	std::error_code ec;
	const std::filesystem::path basePath(path);
	auto it = std::filesystem::recursive_directory_iterator(
		path,
		std::filesystem::directory_options::skip_permission_denied,
		ec);
	const auto end = std::filesystem::recursive_directory_iterator();
	for (; it != end; it.increment(ec)) {
		if (ec) {
			ec.clear();
			continue;
		}

		const auto & entry = *it;
		const std::string filename = entry.path().filename().string();
		if (entry.is_directory(ec)) {
			if (!filename.empty() && filename[0] == '.') {
				it.disable_recursion_pending();
			}
			ec.clear();
			continue;
		}

		const auto entryPath = entry.path();
		const std::string ext = normalizeLower(entryPath.extension().string());
		const bool metadata = isWorkspaceMetadataPath(entryPath);
		bool include = metadata || hasSourceExtension(ext, true);
		if (!preferredExt.empty() && !metadata) {
			include = (ext == preferredExt);
		}
		if (!include) {
			continue;
		}

		hashCombine(&fingerprint, std::hash<std::string>{}(
			normalizeRelativePath(entryPath, basePath)));
		hashCombine(&fingerprint, static_cast<uint64_t>(entry.file_size(ec)));
		ec.clear();
		const auto writeTime = std::filesystem::last_write_time(entryPath, ec);
		if (!ec) {
			hashCombine(&fingerprint, static_cast<uint64_t>(writeTime.time_since_epoch().count()));
		} else {
			ec.clear();
		}
	}

	return fingerprint;
}

ofxGgmlScriptSourceWorkspaceInfo ofxGgmlScriptSource::buildWorkspaceInfoForFolder(
	const std::string & path,
	const ofxGgmlScriptSourceWorkspaceInfo & previousInfo,
	const std::string & requestedVisualStudioPath) const {
	ofxGgmlScriptSourceWorkspaceInfo info;
	info.workspaceRoot = path;
	info.workspaceLabel = std::filesystem::path(path).filename().string();
	if (info.workspaceLabel.empty()) {
		info.workspaceLabel = path;
	}
	info.activeVisualStudioPath = requestedVisualStudioPath.empty()
		? previousInfo.activeVisualStudioPath
		: std::filesystem::path(requestedVisualStudioPath).generic_string();
	info.selectedVisualStudioProjectPath = previousInfo.selectedVisualStudioProjectPath;
	info.hasExplicitVisualStudioProjectSelection =
		previousInfo.hasExplicitVisualStudioProjectSelection;
	info.selectedVisualStudioConfiguration =
		previousInfo.selectedVisualStudioConfiguration.empty()
		? std::string("Release")
		: previousInfo.selectedVisualStudioConfiguration;
	info.selectedVisualStudioPlatform =
		previousInfo.selectedVisualStudioPlatform.empty()
		? std::string("x64")
		: previousInfo.selectedVisualStudioPlatform;
	info.workspaceGeneration = previousInfo.workspaceGeneration + 1;
	info.lastScanTimestampMs = currentTimestampMs();

	std::error_code ec;
	const std::filesystem::path rootPath(path);
	auto it = std::filesystem::recursive_directory_iterator(
		path,
		std::filesystem::directory_options::skip_permission_denied,
		ec);
	const auto end = std::filesystem::recursive_directory_iterator();
	for (; it != end; it.increment(ec)) {
		if (ec) {
			ec.clear();
			continue;
		}
		const auto & entry = *it;
		const auto entryPath = entry.path();
		const std::string filename = normalizeLower(entryPath.filename().string());
		if (entry.is_directory(ec)) {
			if (!filename.empty() && filename[0] == '.') {
				it.disable_recursion_pending();
			}
			ec.clear();
			continue;
		}

		const std::string ext = normalizeLower(entryPath.extension().string());
		const std::string relPath = normalizeRelativePath(entryPath, rootPath);

		if (ext == ".sln" && !info.hasVisualStudioSolution) {
			info.hasVisualStudioSolution = true;
			info.visualStudioSolutionPath = relPath;
		} else if (ext == ".vcxproj") {
			info.visualStudioProjectPaths.push_back(relPath);
		} else if (filename == "compile_commands.json" && !info.hasCompilationDatabase) {
			info.hasCompilationDatabase = true;
			info.compilationDatabasePath = relPath;
		} else if (filename == "cmakelists.txt" && !info.hasCMakeProject) {
			info.hasCMakeProject = true;
			info.cmakeListsPath = relPath;
		} else if (filename == "addons.make" && !info.hasOpenFrameworksProject) {
			info.hasOpenFrameworksProject = true;
			info.addonsMakePath = relPath;
		}
	}

	if (info.hasVisualStudioSolution) {
		const auto solutionInfo = parseVisualStudioSolution(
			rootPath / info.visualStudioSolutionPath,
			rootPath);
		info.visualStudioProjects = solutionInfo.projects;
		if (!solutionInfo.projects.empty()) {
			info.visualStudioProjectPaths.clear();
			for (const auto & project : solutionInfo.projects) {
				info.visualStudioProjectPaths.push_back(project.relativePath);
			}
		}
		info.visualStudioConfigurations = solutionInfo.configurations;
		info.visualStudioPlatforms = solutionInfo.platforms;
	}

	std::sort(
		info.visualStudioProjectPaths.begin(),
		info.visualStudioProjectPaths.end());
	if (info.visualStudioConfigurations.empty()) {
		info.visualStudioConfigurations = {"Debug", "Release"};
	}
	if (info.visualStudioPlatforms.empty()) {
		info.visualStudioPlatforms = {"x64", "Win32"};
	}

	if (info.selectedVisualStudioProjectPath.empty() &&
		!info.visualStudioProjectPaths.empty()) {
		info.selectedVisualStudioProjectPath = info.visualStudioProjectPaths.front();
		info.hasExplicitVisualStudioProjectSelection = false;
	}
	if (!info.selectedVisualStudioProjectPath.empty()) {
		const auto found = std::find(
			info.visualStudioProjectPaths.begin(),
			info.visualStudioProjectPaths.end(),
			info.selectedVisualStudioProjectPath);
		if (found == info.visualStudioProjectPaths.end()) {
			info.selectedVisualStudioProjectPath.clear();
			info.hasExplicitVisualStudioProjectSelection = false;
		}
	}
	if (info.selectedVisualStudioProjectPath.empty() &&
		!info.visualStudioProjectPaths.empty()) {
		info.selectedVisualStudioProjectPath = info.visualStudioProjectPaths.front();
		info.hasExplicitVisualStudioProjectSelection = false;
	}

	if (std::find(
			info.visualStudioConfigurations.begin(),
			info.visualStudioConfigurations.end(),
			info.selectedVisualStudioConfiguration) ==
		info.visualStudioConfigurations.end()) {
		info.selectedVisualStudioConfiguration =
			info.visualStudioConfigurations.front();
	}
	if (std::find(
			info.visualStudioPlatforms.begin(),
			info.visualStudioPlatforms.end(),
			info.selectedVisualStudioPlatform) ==
		info.visualStudioPlatforms.end()) {
		info.selectedVisualStudioPlatform = info.visualStudioPlatforms.front();
	}

	info.defaultBuildDirectory = chooseDefaultBuildDirectory(
		rootPath,
		info.compilationDatabasePath);
#ifdef _WIN32
	info.msbuildPath = resolveMsbuildPathViaVswhere();
	if (info.msbuildPath.empty()) {
		info.msbuildPath = fallbackMsbuildPath();
	}
#endif
	return info;
}

bool ofxGgmlScriptSource::parseGitHubInput(
	const std::string & ownerRepoOrUrl,
	const std::string & branchHint,
	std::string * outOwnerRepo,
	std::string * outBranch,
	std::string * outFocusedPath) {
	const std::string input = trim(ownerRepoOrUrl);
	if (input.empty()) {
		return false;
	}

	std::string ownerRepo = input;
	std::string branch = sanitizeGitHubBranch(branchHint);
	std::string focusedPath;

	const auto isHttpUrl =
		input.rfind("https://", 0) == 0 || input.rfind("http://", 0) == 0;
	if (isHttpUrl) {
		auto stripPrefix = [](const std::string & value, const std::string & prefix) {
			return value.rfind(prefix, 0) == 0
				? value.substr(prefix.size())
				: value;
		};
		std::string normalized = stripPrefix(input, "https://");
		normalized = stripPrefix(normalized, "http://");
		if (normalized.rfind("github.com/", 0) == 0) {
			normalized = normalized.substr(std::string("github.com/").size());
			const size_t queryPos = normalized.find_first_of("?#");
			if (queryPos != std::string::npos) {
				normalized = normalized.substr(0, queryPos);
			}
			std::vector<std::string> parts = splitNonEmpty(normalized, '/');
			if (parts.size() >= 2) {
				ownerRepo = parts[0] + "/" + parts[1];
				if (parts.size() >= 5 &&
					(parts[2] == "tree" || parts[2] == "blob")) {
					if (branch.empty()) {
						branch = parts[3];
					}
					for (size_t i = 4; i < parts.size(); ++i) {
						if (!focusedPath.empty()) {
							focusedPath += "/";
						}
						focusedPath += parts[i];
					}
				} else if (branch.empty() && parts.size() >= 4 && parts[2] == "tree") {
					branch = parts[3];
				}
			}
		} else if (normalized.rfind("raw.githubusercontent.com/", 0) == 0) {
			normalized = normalized.substr(std::string("raw.githubusercontent.com/").size());
			const size_t queryPos = normalized.find_first_of("?#");
			if (queryPos != std::string::npos) {
				normalized = normalized.substr(0, queryPos);
			}
			std::vector<std::string> parts = splitNonEmpty(normalized, '/');
			if (parts.size() >= 3) {
				ownerRepo = parts[0] + "/" + parts[1];
				if (branch.empty()) {
					branch = parts[2];
				}
				for (size_t i = 3; i < parts.size(); ++i) {
					if (!focusedPath.empty()) {
						focusedPath += "/";
					}
					focusedPath += parts[i];
				}
			}
		}
	}

	if (!isValidOwnerRepo(ownerRepo)) {
		return false;
	}
	if (!branch.empty() && !isValidBranch(branch)) {
		return false;
	}
	if (outOwnerRepo != nullptr) {
		*outOwnerRepo = ownerRepo;
	}
	if (outBranch != nullptr) {
		*outBranch = branch;
	}
	if (outFocusedPath != nullptr) {
		*outFocusedPath = std::filesystem::path(focusedPath).generic_string();
	}
	return true;
}

std::string ofxGgmlScriptSource::sanitizeGitHubBranch(const std::string & branchHint) {
	std::string branch = trim(branchHint);
	if (normalizeLower(branch) == "auto") {
		return {};
	}
	return branch;
}

/// Validates GitHub owner/repo format (e.g., "owner/repo").
/// Rejects path traversal, multiple slashes, and non-alphanumeric characters (except -_.).
bool ofxGgmlScriptSource::isValidOwnerRepo(const std::string & ownerRepo) {
	if (ownerRepo.empty() || ownerRepo.find("..") != std::string::npos) return false;
	const size_t slashPos = ownerRepo.find('/');
	if (slashPos == std::string::npos) return false;
	if (ownerRepo.find('/', slashPos + 1) != std::string::npos) return false;
	if (slashPos == 0 || slashPos == ownerRepo.size() - 1) return false;
	for (char c : ownerRepo) {
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '/' && c != '-' && c != '_' && c != '.') {
			return false;
		}
	}
	return true;
}

/// Validates GitHub branch name.
/// Rejects path traversal (..), leading/trailing/double slashes, and control characters.
bool ofxGgmlScriptSource::isValidBranch(const std::string & branch) {
	if (branch.empty()) return false;
	// Reject any path traversal patterns
	if (branch.find("..") != std::string::npos) return false;
	// Reject leading/trailing slashes and double slashes
	if (branch.front() == '/' || branch.back() == '/') return false;
	if (branch.find("//") != std::string::npos) return false;
	// Reject control characters
	for (char c : branch) {
		if (static_cast<unsigned char>(c) < 32) return false;
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '-' && c != '_' && c != '.' && c != '/') {
			return false;
		}
	}
	return true;
}

/// Validates repository file path for safety.
/// Uses canonical path resolution to prevent path traversal attacks including
/// symlink attacks, case confusion, and other filesystem-level bypasses.
/// Rejects absolute paths, backslashes, double slashes, control characters,
/// and null bytes to prevent injection attacks.
bool ofxGgmlScriptSource::isSafeRepoPath(const std::string & path) {
	if (path.empty()) return false;

	// Basic string validation before filesystem operations
	// Reject any path traversal patterns
	if (path.find("..") != std::string::npos) return false;
	// Reject absolute paths and backslashes
	if (path.front() == '/' || path.find('\\') != std::string::npos) return false;
	// Reject double slashes (could indicate path manipulation)
	if (path.find("//") != std::string::npos) return false;
	// Reject all control characters (ASCII < 32)
	for (char c : path) {
		if (static_cast<unsigned char>(c) < 32) return false;
	}
	// Validate that the path doesn't contain any suspicious patterns
	// that could be used for injection
	if (path.find('\0') != std::string::npos) return false;

	// Additional protection: Use weakly_canonical to resolve path components
	// without requiring the path to exist. This catches symlink attacks,
	// case-insensitive ".." patterns, and other filesystem-level bypasses.
	std::error_code ec;
	const std::filesystem::path fsPath(path);
	const std::filesystem::path canonical = std::filesystem::weakly_canonical(fsPath, ec);

	// If canonicalization fails, reject the path
	if (ec) return false;

	// Convert back to string and ensure no ".." components remain
	const std::string canonicalStr = canonical.lexically_normal().string();
	if (canonicalStr.find("..") != std::string::npos) return false;

	// Ensure the canonical path is still relative (not absolute)
	if (canonical.is_absolute()) return false;

	return true;
}

/// Trims leading and trailing whitespace from a string.
std::string ofxGgmlScriptSource::trim(const std::string & s) {
	size_t start = 0;
	while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
	size_t end = s.size();
	while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
	return s.substr(start, end - start);
}

/// Validates URL for internet script sources.
/// Only allows http/https schemes. Rejects control characters, null bytes, whitespace,
/// and shell metacharacters in query strings to prevent injection attacks.
bool ofxGgmlScriptSource::isValidUrl(const std::string & url) {
	if (url.empty()) return false;
	// Reject whitespace characters
	if (url.find(' ') != std::string::npos || url.find('\t') != std::string::npos) return false;
	// Reject control characters
	for (char c : url) {
		if (static_cast<unsigned char>(c) < 32) return false;
	}
	// Reject null bytes
	if (url.find('\0') != std::string::npos) return false;

	const std::string lower = normalizeLower(url);
	// Only allow http and https schemes
	const bool hasValidScheme = (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0);
	if (!hasValidScheme) return false;

	// Ensure URL has content after the scheme
	const size_t schemeEnd = lower.find("://");
	if (schemeEnd == std::string::npos || schemeEnd + 3 >= url.size()) return false;

	// Reject URLs with potentially dangerous characters in query/fragment
	// These could be used for shell injection if URLs are ever passed to shell commands
	const size_t queryStart = url.find('?');
	if (queryStart != std::string::npos) {
		const std::string query = url.substr(queryStart);
		// Reject shell metacharacters in query parameters
		if (query.find(';') != std::string::npos ||
		    query.find('`') != std::string::npos ||
		    query.find('$') != std::string::npos ||
		    query.find('|') != std::string::npos) {
			return false;
		}
	}

	return true;
}

bool ofxGgmlScriptSource::hasSourceExtension(
	const std::string & ext, bool includeTextLikeExtensions) {
	static constexpr const char* sourceExts[] = {
		".cpp", ".h", ".py", ".js", ".ts", ".rs", ".go",
		".glsl", ".vert", ".frag", ".sh", ".c", ".hpp",
		".java", ".kt", ".swift", ".lua", ".rb", ".cs"
	};
	static constexpr const char* textLikeExts[] = {
		".md", ".txt", ".json", ".yaml", ".yml", ".toml"
	};
	for (const char* candidate : sourceExts) {
		if (ext == candidate) return true;
	}
	if (includeTextLikeExtensions) {
		for (const char* candidate : textLikeExts) {
			if (ext == candidate) return true;
		}
	}
	return false;
}
