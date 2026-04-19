#include "ofxGgmlWebCrawler.h"

#include "core/ofxGgmlWindowsUtf8.h"
#include "support/ofxGgmlProcessSecurity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

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

std::string toLowerCopy(const std::string & text) {
	std::string lowered = text;
	std::transform(
		lowered.begin(),
		lowered.end(),
		lowered.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lowered;
}

std::string stripTrailingDot(std::string value) {
	while (!value.empty() && value.back() == '.') {
		value.pop_back();
	}
	return value;
}

std::string parseUrlHost(const std::string & rawUrl) {
	const std::string trimmed = trimCopy(rawUrl);
	const size_t schemePos = trimmed.find("://");
	if (schemePos == std::string::npos) {
		return {};
	}
	const size_t hostStart = schemePos + 3;
	size_t hostEnd = trimmed.find_first_of("/?#", hostStart);
	if (hostEnd == std::string::npos) {
		hostEnd = trimmed.size();
	}
	std::string hostPort = trimmed.substr(hostStart, hostEnd - hostStart);
	const size_t atPos = hostPort.rfind('@');
	if (atPos != std::string::npos) {
		hostPort = hostPort.substr(atPos + 1);
	}
	if (!hostPort.empty() && hostPort.front() == '[') {
		const size_t closing = hostPort.find(']');
		if (closing != std::string::npos) {
			return stripTrailingDot(toLowerCopy(hostPort.substr(0, closing + 1)));
		}
	}
	const size_t colonPos = hostPort.find(':');
	if (colonPos != std::string::npos) {
		hostPort = hostPort.substr(0, colonPos);
	}
	return stripTrailingDot(toLowerCopy(hostPort));
}

bool hostMatchesAllowedDomain(
	const std::string & host,
	const std::string & allowedDomain) {
	const std::string normalizedHost = stripTrailingDot(toLowerCopy(host));
	std::string normalizedDomain = stripTrailingDot(toLowerCopy(trimCopy(allowedDomain)));
	if (normalizedHost.empty() || normalizedDomain.empty()) {
		return false;
	}
	if (normalizedDomain.rfind("*.", 0) == 0) {
		normalizedDomain = normalizedDomain.substr(2);
	}
	if (normalizedHost == normalizedDomain) {
		return true;
	}
	if (normalizedHost.size() <= normalizedDomain.size()) {
		return false;
	}
	return normalizedHost.compare(
			normalizedHost.size() - normalizedDomain.size(),
			normalizedDomain.size(),
			normalizedDomain) == 0 &&
		normalizedHost[normalizedHost.size() - normalizedDomain.size() - 1] == '.';
}

bool isUrlAllowedForDomains(
	const std::string & rawUrl,
	const std::vector<std::string> & allowedDomains) {
	if (allowedDomains.empty()) {
		return true;
	}
	const std::string host = parseUrlHost(rawUrl);
	if (host.empty()) {
		return false;
	}
	return std::any_of(
		allowedDomains.begin(),
		allowedDomains.end(),
		[&](const std::string & domain) {
			return hostMatchesAllowedDomain(host, domain);
		});
}

std::string extractSourceUrlFromMarkdown(const std::string & markdown) {
	std::istringstream stream(markdown);
	std::string line;
	bool inFrontMatter = false;
	bool frontMatterConsumed = false;
	while (std::getline(stream, line)) {
		const std::string trimmed = trimCopy(line);
		if (!frontMatterConsumed && trimmed == "---") {
			inFrontMatter = !inFrontMatter;
			frontMatterConsumed = inFrontMatter;
			continue;
		}
		if (inFrontMatter) {
			const std::array<std::string, 3> keys = {
				"url:",
				"source_url:",
				"source:"
			};
			for (const auto & key : keys) {
				if (trimmed.rfind(key, 0) == 0) {
					return trimCopy(trimmed.substr(key.size()));
				}
			}
			continue;
		}
		if (trimmed.empty()) {
			continue;
		}
		if (trimmed.rfind("URL:", 0) == 0 || trimmed.rfind("Url:", 0) == 0) {
			return trimCopy(trimmed.substr(4));
		}
		if (trimmed.rfind("Source URL:", 0) == 0) {
			return trimCopy(trimmed.substr(11));
		}
		if (trimmed.front() == '#') {
			continue;
		}
		break;
	}
	return {};
}

float elapsedMsSince(const std::chrono::steady_clock::time_point & start) {
	return std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
}

std::string makeTempOutputDir() {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path(ec);
	}

	std::random_device rd;
	std::mt19937_64 rng(rd());
	std::uniform_int_distribution<uint64_t> dist;

	for (int attempt = 0; attempt < 128; ++attempt) {
		const std::filesystem::path candidate =
			base / ("ofxggml_mojo_" + std::to_string(dist(rng)));
		if (std::filesystem::create_directories(candidate, ec) && !ec) {
			return candidate.string();
		}
		ec.clear();
	}
	return {};
}

std::string extractMarkdownTitle(const std::string & markdown) {
	std::istringstream stream(markdown);
	std::string line;
	while (std::getline(stream, line)) {
		const std::string trimmed = trimCopy(line);
		if (trimmed.empty()) {
			continue;
		}
		if (!trimmed.empty() && trimmed[0] == '#') {
			size_t pos = 0;
			while (pos < trimmed.size() && trimmed[pos] == '#') {
				++pos;
			}
			return trimCopy(trimmed.substr(pos));
		}
	}
	return {};
}

bool readTextFile(
	const std::filesystem::path & path,
	std::string * textOut,
	size_t * sizeOut) {
	if (!textOut || !sizeOut) {
		return false;
	}
	std::ifstream input(path, std::ios::binary);
	if (!input.is_open()) {
		return false;
	}
	std::ostringstream buffer;
	buffer << input.rdbuf();
	*textOut = buffer.str();
	*sizeOut = textOut->size();
	return true;
}

std::vector<std::filesystem::path> collectMarkdownFiles(
	const std::filesystem::path & outputDir) {
	std::vector<std::filesystem::path> files;
	std::error_code ec;
	if (!std::filesystem::exists(outputDir, ec) || ec) {
		return files;
	}

	for (std::filesystem::recursive_directory_iterator it(outputDir, ec), end;
		!ec && it != end;
		it.increment(ec)) {
		if (ec || !it->is_regular_file()) {
			continue;
		}
		std::string ext = it->path().extension().string();
		std::transform(
			ext.begin(),
			ext.end(),
			ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (ext == ".md" || ext == ".markdown" || ext == ".txt") {
			files.push_back(it->path());
		}
	}

	std::sort(files.begin(), files.end());
	return files;
}

std::filesystem::path resolveAddonRootFromSourceFile() {
	std::error_code ec;
	const std::filesystem::path sourcePath =
		std::filesystem::weakly_canonical(std::filesystem::path(__FILE__), ec);
	if (ec) {
		return {};
	}

	std::filesystem::path root = sourcePath.parent_path();
	for (int i = 0; i < 2 && !root.empty(); ++i) {
		root = root.parent_path();
	}
	return root;
}

std::string resolveMojoExecutable(const std::string & requestedPath) {
	const std::vector<std::string> candidates = [&]() {
		std::vector<std::string> values;
		if (!trimCopy(requestedPath).empty()) {
			values.push_back(trimCopy(requestedPath));
		}

		const std::filesystem::path addonRoot = resolveAddonRootFromSourceFile();
#ifdef _WIN32
		values.push_back("libs/mojo/bin/mojo.bat");
		values.push_back("libs/mojo/bin/mojo.cmd");
		values.push_back("libs/mojo/bin/mojo.exe");
		if (!addonRoot.empty()) {
			values.push_back((addonRoot / "libs/mojo/bin/mojo.bat").string());
			values.push_back((addonRoot / "libs/mojo/bin/mojo.cmd").string());
			values.push_back((addonRoot / "libs/mojo/bin/mojo.exe").string());
		}
		values.push_back("mojo.bat");
		values.push_back("mojo.cmd");
		values.push_back("mojo.exe");
#else
		values.push_back("libs/mojo/bin/mojo");
		if (!addonRoot.empty()) {
			values.push_back((addonRoot / "libs/mojo/bin/mojo").string());
		}
		values.push_back("mojo");
#endif
		return values;
	}();

	for (const auto & candidate : candidates) {
		if (ofxGgmlProcessSecurity::isValidExecutablePath(candidate)) {
			return candidate;
		}
	}
	return {};
}

bool runProcessCapture(
	const std::vector<std::string> & args,
	const std::string & workingDirectory,
	std::string * output,
	int * exitCode) {
	if (output) {
		output->clear();
	}
	if (exitCode) {
		*exitCode = -1;
	}
	if (args.empty() || args.front().empty()) {
		return false;
	}

#ifdef _WIN32
	SECURITY_ATTRIBUTES sa {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = nullptr;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
		return false;
	}
	if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
		CloseHandle(readPipe);
		CloseHandle(writePipe);
		return false;
	}

	STARTUPINFOW si {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	HANDLE nullInput = CreateFileA(
		"NUL",
		GENERIC_READ,
		0,
		&sa,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	si.hStdInput = (nullInput != INVALID_HANDLE_VALUE)
		? nullInput
		: GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = writePipe;
	si.hStdError = writePipe;

	const std::string cmdLine =
		ofxGgmlProcessSecurity::buildWindowsCommandLine(args);
	if (cmdLine.empty()) {
		CloseHandle(readPipe);
		CloseHandle(writePipe);
		if (nullInput != INVALID_HANDLE_VALUE) {
			CloseHandle(nullInput);
		}
		return false;
	}

	std::wstring wideCmdLine = ofxGgmlWideFromUtf8(cmdLine);
	std::wstring wideCwd = ofxGgmlWideFromUtf8(workingDirectory);
	PROCESS_INFORMATION pi {};
	const BOOL created = CreateProcessW(
		nullptr,
		wideCmdLine.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		wideCwd.empty() ? nullptr : wideCwd.c_str(),
		&si,
		&pi);

	CloseHandle(writePipe);
	if (nullInput != INVALID_HANDLE_VALUE) {
		CloseHandle(nullInput);
	}
	if (!created) {
		CloseHandle(readPipe);
		return false;
	}

	std::array<char, 4096> buffer {};
	DWORD bytesRead = 0;
	while (ReadFile(
		readPipe,
		buffer.data(),
		static_cast<DWORD>(buffer.size()),
		&bytesRead,
		nullptr) &&
		bytesRead > 0) {
		if (output) {
			output->append(buffer.data(), static_cast<size_t>(bytesRead));
		}
	}
	CloseHandle(readPipe);

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD processExitCode = 0;
	GetExitCodeProcess(pi.hProcess, &processExitCode);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if (exitCode) {
		*exitCode = static_cast<int>(processExitCode);
	}
	return true;
#else
	int pipeFds[2] = {-1, -1};
	if (pipe(pipeFds) != 0) {
		return false;
	}

	const pid_t pid = fork();
	if (pid < 0) {
		close(pipeFds[0]);
		close(pipeFds[1]);
		return false;
	}
	if (pid == 0) {
		close(pipeFds[0]);
		dup2(pipeFds[1], STDOUT_FILENO);
		dup2(pipeFds[1], STDERR_FILENO);
		close(pipeFds[1]);
		if (!workingDirectory.empty()) {
			chdir(workingDirectory.c_str());
		}

		std::vector<char *> argv;
		argv.reserve(args.size() + 1);
		for (const auto & arg : args) {
			argv.push_back(const_cast<char *>(arg.c_str()));
		}
		argv.push_back(nullptr);
		execvp(argv[0], argv.data());
		_exit(127);
	}

	close(pipeFds[1]);
	std::array<char, 4096> buffer {};
	ssize_t bytesRead = 0;
	while ((bytesRead = read(pipeFds[0], buffer.data(), buffer.size())) > 0) {
		if (output) {
			output->append(buffer.data(), static_cast<size_t>(bytesRead));
		}
	}
	close(pipeFds[0]);

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		return false;
	}
	if (exitCode) {
		if (WIFEXITED(status)) {
			*exitCode = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			*exitCode = 128 + WTERMSIG(status);
		}
	}
	return true;
#endif
}

std::string buildNormalizedCommand(const std::vector<std::string> & args) {
	std::ostringstream out;
	for (size_t i = 0; i < args.size(); ++i) {
		if (i > 0) {
			out << ' ';
		}
		const bool needsQuotes =
			args[i].find_first_of(" \t\"") != std::string::npos;
		if (needsQuotes) {
			out << '"' << args[i] << '"';
		} else {
			out << args[i];
		}
	}
	return out.str();
}

} // namespace

ofxGgmlWebCrawlerBridgeBackend::ofxGgmlWebCrawlerBridgeBackend(
	CrawlCallback callback)
	: m_callback(std::move(callback)) {}

std::string ofxGgmlWebCrawlerBridgeBackend::backendName() const {
	return "Bridge";
}

ofxGgmlWebCrawlerResult ofxGgmlWebCrawlerBridgeBackend::crawl(
	const ofxGgmlWebCrawlerRequest & request) const {
	if (!m_callback) {
		return {
			false,
			0.0f,
			backendName(),
			request.startUrl,
			request.outputDir,
			"",
			"",
			"Web crawler bridge callback is not configured.",
			-1,
			{},
			{}
		};
	}
	return m_callback(request);
}

ofxGgmlMojoWebCrawlerBackend::ofxGgmlMojoWebCrawlerBackend(
	const std::string & executablePath)
	: m_executablePath(executablePath) {}

void ofxGgmlMojoWebCrawlerBackend::setExecutablePath(const std::string & path) {
	m_executablePath = path;
}

const std::string & ofxGgmlMojoWebCrawlerBackend::getExecutablePath() const {
	return m_executablePath;
}

std::string ofxGgmlMojoWebCrawlerBackend::backendName() const {
	return "Mojo";
}

ofxGgmlWebCrawlerResult ofxGgmlMojoWebCrawlerBackend::crawl(
	const ofxGgmlWebCrawlerRequest & request) const {
	using Clock = std::chrono::steady_clock;
	const auto start = Clock::now();

	ofxGgmlWebCrawlerResult result;
	result.backendName = backendName();
	result.startUrl = trimCopy(request.startUrl);

	if (result.startUrl.empty()) {
		result.error = "Crawler start URL is empty.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	const std::string executable =
		resolveMojoExecutable(
			trimCopy(request.executablePath).empty()
				? m_executablePath
				: request.executablePath);
	if (executable.empty()) {
		result.error =
			"Mojo wrapper or executable was not found. Set executablePath or install Mojo. "
			"On Windows, use scripts/install-mojo.ps1 to create libs/mojo/bin/mojo.bat "
			"for the local WSL-backed setup.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	bool createdTempDir = false;
	result.outputDir = trimCopy(request.outputDir);
	if (result.outputDir.empty()) {
		result.outputDir = makeTempOutputDir();
		createdTempDir = true;
	}
	if (result.outputDir.empty()) {
		result.error = "Could not create crawler output directory.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	std::error_code ec;
	std::filesystem::create_directories(result.outputDir, ec);
	if (ec) {
		result.error =
			"Could not create crawler output directory: " + result.outputDir;
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	std::vector<std::string> args;
	args.push_back(executable);
	args.push_back("-d");
	args.push_back(std::to_string(std::max(0, request.maxDepth)));
	args.push_back("-o");
	args.push_back(result.outputDir);
	if (request.renderJavaScript) {
		args.push_back("--render");
	}
	for (const auto & extraArg : request.extraArgs) {
		if (!trimCopy(extraArg).empty()) {
			args.push_back(extraArg);
		}
	}
	args.push_back(result.startUrl);

	result.normalizedCommand = buildNormalizedCommand(args);
	if (!request.allowedDomains.empty() &&
		!isUrlAllowedForDomains(result.startUrl, request.allowedDomains)) {
		result.error =
			"Crawler start URL is outside the allowedDomains restriction.";
		result.elapsedMs = elapsedMsSince(start);
		if (createdTempDir && !request.keepOutputFiles) {
			std::filesystem::remove_all(result.outputDir, ec);
		}
		return result;
	}
	const std::string workingDirectory =
		std::filesystem::path(result.outputDir).parent_path().string();
	if (!runProcessCapture(
		args,
		workingDirectory,
		&result.commandOutput,
		&result.exitCode)) {
		result.error = "Failed to launch Mojo crawler process.";
		result.elapsedMs = elapsedMsSince(start);
		if (createdTempDir && !request.keepOutputFiles) {
			std::filesystem::remove_all(result.outputDir, ec);
		}
		return result;
	}

	if (result.exitCode != 0) {
		result.error =
			"Mojo exited with code " + std::to_string(result.exitCode) + ".";
		result.elapsedMs = elapsedMsSince(start);
		if (createdTempDir && !request.keepOutputFiles) {
			std::filesystem::remove_all(result.outputDir, ec);
		}
		return result;
	}

	const auto markdownFiles = collectMarkdownFiles(result.outputDir);
	for (const auto & markdownPath : markdownFiles) {
		std::string markdown;
		size_t byteSize = 0;
		if (!readTextFile(markdownPath, &markdown, &byteSize)) {
			continue;
		}

		ofxGgmlCrawledDocument document;
		document.localPath = markdownPath.string();
		document.markdown = std::move(markdown);
		document.byteSize = byteSize;
		document.sourceUrl = extractSourceUrlFromMarkdown(document.markdown);
		if (document.sourceUrl.empty()) {
			document.sourceUrl = result.startUrl;
		}
		document.crawlDepth = 0;
		document.title = extractMarkdownTitle(document.markdown);
		if (document.title.empty()) {
			document.title = markdownPath.stem().string();
		}
		if (!request.allowedDomains.empty() &&
			!isUrlAllowedForDomains(document.sourceUrl, request.allowedDomains)) {
			continue;
		}
		result.savedFiles.push_back(document.localPath);
		result.documents.push_back(std::move(document));
	}

	if (result.documents.empty()) {
		result.error =
			"Mojo completed but no Markdown files were found in the output directory.";
		result.elapsedMs = elapsedMsSince(start);
		if (createdTempDir && !request.keepOutputFiles) {
			std::filesystem::remove_all(result.outputDir, ec);
		}
		return result;
	}

	result.success = true;
	result.elapsedMs = elapsedMsSince(start);
	if (!request.allowedDomains.empty()) {
		if (!result.commandOutput.empty() &&
			result.commandOutput.back() != '\n') {
			result.commandOutput.push_back('\n');
		}
		result.commandOutput +=
			"Note: default Mojo integration validates the start URL and filters "
			"normalized results by allowedDomains, but full crawl-time domain "
			"restriction still depends on backend-specific CLI support.\n";
	}
	if (createdTempDir && !request.keepOutputFiles) {
		std::filesystem::remove_all(result.outputDir, ec);
		result.savedFiles.clear();
		result.outputDir.clear();
		for (auto & document : result.documents) {
			document.localPath.clear();
		}
	}
	return result;
}

std::shared_ptr<ofxGgmlWebCrawlerBackend>
createWebCrawlerBridgeBackend(
	ofxGgmlWebCrawlerBridgeBackend::CrawlCallback callback) {
	return std::make_shared<ofxGgmlWebCrawlerBridgeBackend>(std::move(callback));
}

ofxGgmlWebCrawler::ofxGgmlWebCrawler()
	: m_backend(std::make_shared<ofxGgmlMojoWebCrawlerBackend>()) {}

void ofxGgmlWebCrawler::setBackend(
	std::shared_ptr<ofxGgmlWebCrawlerBackend> backend) {
	m_backend = std::move(backend);
}

std::shared_ptr<ofxGgmlWebCrawlerBackend>
ofxGgmlWebCrawler::getBackend() const {
	return m_backend;
}

ofxGgmlWebCrawlerResult ofxGgmlWebCrawler::crawl(
	const ofxGgmlWebCrawlerRequest & request) const {
	if (!m_backend) {
		return {
			false,
			0.0f,
			"",
			request.startUrl,
			request.outputDir,
			"",
			"",
			"Web crawler backend is not configured.",
			-1,
			{},
			{}
		};
	}
	return m_backend->crawl(request);
}
