#include "ofxGgmlWebCrawler.h"

#include "core/ofxGgmlWindowsUtf8.h"
#include "support/ofxGgmlProcessSecurity.h"

#if defined(__has_include)
#if __has_include(<libxml/parser.h>) && __has_include(<libxml/xpath.h>)
#include <libxml/parser.h>
#include <libxml/xpath.h>
#define OFXGGML_HAS_LIBXML2 1
#elif __has_include("../../libs/libxml2/include/libxml/parser.h") && __has_include("../../libs/libxml2/include/libxml/xpath.h")
#include "../../libs/libxml2/include/libxml/parser.h"
#include "../../libs/libxml2/include/libxml/xpath.h"
#define OFXGGML_HAS_LIBXML2 1
#else
#define OFXGGML_HAS_LIBXML2 0
#endif
#else
#define OFXGGML_HAS_LIBXML2 0
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <queue>
#include <random>
#include <regex>
#include <sstream>
#include <system_error>
#include <unordered_set>
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
#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if OFXGGML_HAS_LIBXML2
using ofxGgmlXmlReadMemoryFn =
	xmlDocPtr (*)(const char *, int, const char *, const char *, int);
using ofxGgmlHtmlReadMemoryFn =
	xmlDocPtr (*)(const char *, int, const char *, const char *, int);
using ofxGgmlXmlInitParserFn = void (*)();
using ofxGgmlXmlDocGetRootElementFn = xmlNodePtr (*)(const xmlDoc *);
using ofxGgmlXmlFreeDocFn = void (*)(xmlDocPtr);
using ofxGgmlXmlNodeGetContentFn = xmlChar * (*)(const xmlNode *);
using ofxGgmlXmlGetPropFn = xmlChar * (*)(const xmlNode *, const xmlChar *);
using ofxGgmlXmlFreeFn = void (*)(void *);
using ofxGgmlXmlXPathNewContextFn = xmlXPathContextPtr (*)(xmlDocPtr);
using ofxGgmlXmlXPathEvalExpressionFn =
	xmlXPathObjectPtr (*)(const xmlChar *, xmlXPathContextPtr);
using ofxGgmlXmlXPathFreeObjectFn = void (*)(xmlXPathObjectPtr);
using ofxGgmlXmlXPathFreeContextFn = void (*)(xmlXPathContextPtr);

constexpr int kOfxGgmlXmlParseRecover = XML_PARSE_RECOVER;
constexpr int kOfxGgmlXmlParseNoError = XML_PARSE_NOERROR;
constexpr int kOfxGgmlXmlParseNoWarning = XML_PARSE_NOWARNING;
constexpr int kOfxGgmlXmlParseNoNet = XML_PARSE_NONET;
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

std::string resolveCurlExecutable() {
#ifdef _WIN32
	const std::vector<std::string> candidates = {"curl.exe", "curl"};
#else
	const std::vector<std::string> candidates = {"/usr/bin/curl", "/usr/local/bin/curl", "curl"};
#endif
	const std::string envCurl =
		ofxGgmlProcessSecurity::getEnvVarString("OFXGGML_CURL");
	if (!envCurl.empty() && ofxGgmlProcessSecurity::isValidExecutablePath(envCurl)) {
		return envCurl;
	}
	for (const auto & candidate : candidates) {
		if (ofxGgmlProcessSecurity::isValidExecutablePath(candidate)) {
			return candidate;
		}
	}
#ifdef _WIN32
	return "curl.exe";
#else
	return "curl";
#endif
}

std::string resolveXmllintExecutable() {
	const std::string envXmllint =
		ofxGgmlProcessSecurity::getEnvVarString("OFXGGML_XMLLINT");
	if (!envXmllint.empty() &&
		ofxGgmlProcessSecurity::isValidExecutablePath(envXmllint)) {
		return envXmllint;
	}

	const std::filesystem::path addonRoot = resolveAddonRootFromSourceFile();
	std::vector<std::string> candidates;
	if (!addonRoot.empty()) {
		candidates.push_back(
			(addonRoot / "libs/libxml2/bin/xmllint.exe").string());
		candidates.push_back(
			(addonRoot / "build/vendor/libxml2-install/bin/xmllint.exe").string());
	}
#ifdef _WIN32
	candidates.push_back("xmllint.exe");
#else
	candidates.push_back("xmllint");
#endif

	for (const auto & candidate : candidates) {
		if (ofxGgmlProcessSecurity::isValidExecutablePath(candidate)) {
			return candidate;
		}
	}
	return {};
}

struct NativeHtmlNormalizationResult {
	bool success = false;
	std::string normalizedHtml;
	std::string commandOutput;
	std::string error;
};

NativeHtmlNormalizationResult normalizeHtmlWithXmllint(
	const std::string & htmlText) {
	NativeHtmlNormalizationResult result;
	const std::string xmllintExe = resolveXmllintExecutable();
	if (xmllintExe.empty()) {
		result.error = "xmllint executable was not found for libxml2 HTML parsing.";
		return result;
	}
	if (htmlText.empty()) {
		result.error = "HTML input was empty.";
		return result;
	}

	const std::string tempDir = makeTempOutputDir();
	if (tempDir.empty()) {
		result.error = "Could not create a temporary directory for xmllint output.";
		return result;
	}

	const std::filesystem::path inputPath =
		std::filesystem::path(tempDir) / "input.html";
	const std::filesystem::path outputPath =
		std::filesystem::path(tempDir) / "normalized.html";
	{
		std::ofstream out(inputPath, std::ios::binary);
		if (!out.is_open()) {
			std::error_code ec;
			std::filesystem::remove_all(tempDir, ec);
			result.error = "Could not write temporary HTML input for xmllint.";
			return result;
		}
		out << htmlText;
	}

	std::vector<std::string> args = {
		xmllintExe,
		"--html",
		"--recover",
		"--format",
		"--output",
		outputPath.string(),
		inputPath.string()
	};
	int exitCode = -1;
	runProcessCapture(args, tempDir, &result.commandOutput, &exitCode);

	size_t outputSize = 0;
	readTextFile(outputPath, &result.normalizedHtml, &outputSize);
	std::error_code cleanupEc;
	std::filesystem::remove_all(tempDir, cleanupEc);

	if (!result.normalizedHtml.empty()) {
		result.success = true;
		return result;
	}

	result.error = "xmllint did not produce normalized HTML output.";
	const std::string trimmedOutput = trimCopy(result.commandOutput);
	if (!trimmedOutput.empty()) {
		result.error += " " + trimmedOutput;
	}
	return result;
}

#if OFXGGML_HAS_LIBXML2
struct LibXml2Api {
	bool available = false;
	std::string loadError;
#ifdef _WIN32
	HMODULE handle = nullptr;
#else
	void * handle = nullptr;
#endif
	ofxGgmlXmlReadMemoryFn xmlReadMemoryFn = nullptr;
	ofxGgmlHtmlReadMemoryFn htmlReadMemoryFn = nullptr;
	ofxGgmlXmlInitParserFn xmlInitParserFn = nullptr;
	ofxGgmlXmlDocGetRootElementFn xmlDocGetRootElementFn = nullptr;
	ofxGgmlXmlFreeDocFn xmlFreeDocFn = nullptr;
	ofxGgmlXmlNodeGetContentFn xmlNodeGetContentFn = nullptr;
	ofxGgmlXmlGetPropFn xmlGetPropFn = nullptr;
	ofxGgmlXmlFreeFn xmlFreeFn = nullptr;
	ofxGgmlXmlXPathNewContextFn xmlXPathNewContextFn = nullptr;
	ofxGgmlXmlXPathEvalExpressionFn xmlXPathEvalExpressionFn = nullptr;
	ofxGgmlXmlXPathFreeObjectFn xmlXPathFreeObjectFn = nullptr;
	ofxGgmlXmlXPathFreeContextFn xmlXPathFreeContextFn = nullptr;
};

std::vector<std::string> candidateLibXml2Libraries() {
	const std::filesystem::path addonRoot = resolveAddonRootFromSourceFile();
	std::vector<std::string> candidates;
#ifdef _WIN32
	if (!addonRoot.empty()) {
		candidates.push_back(
			(addonRoot /
				"libs/libxml2/lib/vs/x64/libxml2.dll").string());
	}
	candidates.push_back("libxml2.dll");
#elif defined(__APPLE__)
	candidates.push_back("libxml2.dylib");
	candidates.push_back("/usr/local/lib/libxml2.dylib");
	candidates.push_back("/opt/homebrew/lib/libxml2.dylib");
#else
	candidates.push_back("libxml2.so");
	candidates.push_back("libxml2.so.2");
#endif
	return candidates;
}

const LibXml2Api & getLibXml2Api() {
	static const LibXml2Api api = []() {
		LibXml2Api loaded;
#ifdef _WIN32
		for (const auto & candidate : candidateLibXml2Libraries()) {
			const std::wstring wide = ofxGgmlWideFromUtf8(candidate);
			HMODULE handle = LoadLibraryW(wide.c_str());
			if (!handle) {
				continue;
			}
			loaded.handle = handle;
			break;
		}
		auto resolve = [&](const char * symbol) -> FARPROC {
			return loaded.handle ? GetProcAddress(loaded.handle, symbol) : nullptr;
		};
#else
		for (const auto & candidate : candidateLibXml2Libraries()) {
			void * handle = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
			if (!handle) {
				continue;
			}
			loaded.handle = handle;
			break;
		}
		auto resolve = [&](const char * symbol) -> void * {
			return loaded.handle ? dlsym(loaded.handle, symbol) : nullptr;
		};
#endif
		if (!loaded.handle) {
			loaded.loadError =
				"libxml2 runtime was not found. Expected a bundled libxml2 such as "
				"addons/ofxGgml/libs/libxml2/lib/vs/x64/libxml2.dll.";
			return loaded;
		}

		loaded.xmlReadMemoryFn =
			reinterpret_cast<decltype(loaded.xmlReadMemoryFn)>(resolve("xmlReadMemory"));
		loaded.htmlReadMemoryFn =
			reinterpret_cast<decltype(loaded.htmlReadMemoryFn)>(resolve("htmlReadMemory"));
		loaded.xmlInitParserFn =
			reinterpret_cast<decltype(loaded.xmlInitParserFn)>(resolve("xmlInitParser"));
		loaded.xmlDocGetRootElementFn =
			reinterpret_cast<decltype(loaded.xmlDocGetRootElementFn)>(resolve("xmlDocGetRootElement"));
		loaded.xmlFreeDocFn =
			reinterpret_cast<decltype(loaded.xmlFreeDocFn)>(resolve("xmlFreeDoc"));
		loaded.xmlNodeGetContentFn =
			reinterpret_cast<decltype(loaded.xmlNodeGetContentFn)>(resolve("xmlNodeGetContent"));
		loaded.xmlGetPropFn =
			reinterpret_cast<decltype(loaded.xmlGetPropFn)>(resolve("xmlGetProp"));
		loaded.xmlFreeFn =
			reinterpret_cast<decltype(loaded.xmlFreeFn)>(resolve("xmlFree"));
		loaded.xmlXPathNewContextFn =
			reinterpret_cast<decltype(loaded.xmlXPathNewContextFn)>(resolve("xmlXPathNewContext"));
		loaded.xmlXPathEvalExpressionFn =
			reinterpret_cast<decltype(loaded.xmlXPathEvalExpressionFn)>(resolve("xmlXPathEvalExpression"));
		loaded.xmlXPathFreeObjectFn =
			reinterpret_cast<decltype(loaded.xmlXPathFreeObjectFn)>(resolve("xmlXPathFreeObject"));
		loaded.xmlXPathFreeContextFn =
			reinterpret_cast<decltype(loaded.xmlXPathFreeContextFn)>(resolve("xmlXPathFreeContext"));

		const bool complete =
			loaded.xmlReadMemoryFn &&
			loaded.xmlInitParserFn &&
			loaded.xmlDocGetRootElementFn &&
			loaded.xmlFreeDocFn &&
			loaded.xmlNodeGetContentFn &&
			loaded.xmlGetPropFn &&
			loaded.xmlFreeFn &&
			loaded.xmlXPathNewContextFn &&
			loaded.xmlXPathEvalExpressionFn &&
			loaded.xmlXPathFreeObjectFn &&
			loaded.xmlXPathFreeContextFn;
		if (!complete) {
			loaded.loadError =
				"libxml2 runtime loaded, but one or more required XML/XPath symbols were missing.";
			return loaded;
		}

		loaded.xmlInitParserFn();
		loaded.available = true;
		return loaded;
	}();
	return api;
}
#endif

bool isLikelyHtmlContentType(const std::string & contentType) {
	if (contentType.empty()) {
		return true;
	}
	const std::string lowered = toLowerCopy(contentType);
	return lowered.find("text/html") != std::string::npos ||
		lowered.find("application/xhtml+xml") != std::string::npos ||
		lowered.find("application/xml") != std::string::npos;
}

bool isFileUrl(const std::string & url) {
	return toLowerCopy(url).rfind("file://", 0) == 0;
}

std::string stripUrlFragment(const std::string & url) {
	const size_t fragmentPos = url.find('#');
	return fragmentPos == std::string::npos ? url : url.substr(0, fragmentPos);
}

std::string sanitizeFileStem(std::string value) {
	for (char & ch : value) {
		const unsigned char uch = static_cast<unsigned char>(ch);
		if (!std::isalnum(uch) && ch != '-' && ch != '_') {
			ch = '_';
		}
	}
	while (!value.empty() && value.front() == '_') {
		value.erase(value.begin());
	}
	while (!value.empty() && value.back() == '_') {
		value.pop_back();
	}
	return value.empty() ? "page" : value;
}

std::string normalizedFileStemFromUrl(const std::string & url, size_t index) {
	const std::string stripped = stripUrlFragment(url);
	const size_t slashPos = stripped.find_last_of('/');
	std::string tail = slashPos == std::string::npos
		? stripped
		: stripped.substr(slashPos + 1);
	const size_t queryPos = tail.find('?');
	if (queryPos != std::string::npos) {
		tail = tail.substr(0, queryPos);
	}
	if (tail.empty() || tail == "." || tail == "..") {
		tail = "page_" + std::to_string(index + 1);
	}
	return sanitizeFileStem(std::filesystem::path(tail).stem().string());
}

std::string normalizeWhitespace(const std::string & text) {
	std::string normalized;
	normalized.reserve(text.size());
	bool previousWasSpace = false;
	for (const unsigned char ch : text) {
		if (std::isspace(ch)) {
			if (!previousWasSpace) {
				normalized.push_back(' ');
			}
			previousWasSpace = true;
		} else {
			normalized.push_back(static_cast<char>(ch));
			previousWasSpace = false;
		}
	}
	return trimCopy(normalized);
}

std::string resolveRelativeUrl(
	const std::string & baseUrl,
	const std::string & href);

void replaceAllInPlace(
	std::string & text,
	const std::string & from,
	const std::string & to) {
	if (from.empty()) {
		return;
	}
	size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::string::npos) {
		text.replace(pos, from.size(), to);
		pos += to.size();
	}
}

std::string decodeBasicHtmlEntities(std::string text) {
	// Named entities (most common ones)
	replaceAllInPlace(text, "&nbsp;", " ");
	replaceAllInPlace(text, "&amp;", "&");
	replaceAllInPlace(text, "&lt;", "<");
	replaceAllInPlace(text, "&gt;", ">");
	replaceAllInPlace(text, "&quot;", "\"");
	replaceAllInPlace(text, "&#39;", "'");
	replaceAllInPlace(text, "&apos;", "'");

	// Additional common entities
	replaceAllInPlace(text, "&ndash;", "–");
	replaceAllInPlace(text, "&mdash;", "—");
	replaceAllInPlace(text, "&hellip;", "…");
	replaceAllInPlace(text, "&laquo;", "«");
	replaceAllInPlace(text, "&raquo;", "»");
	replaceAllInPlace(text, "&ldquo;", """);
	replaceAllInPlace(text, "&rdquo;", """);
	replaceAllInPlace(text, "&lsquo;", "'");
	replaceAllInPlace(text, "&rsquo;", "'");
	replaceAllInPlace(text, "&bull;", "•");
	replaceAllInPlace(text, "&deg;", "°");
	replaceAllInPlace(text, "&copy;", "©");
	replaceAllInPlace(text, "&reg;", "®");
	replaceAllInPlace(text, "&trade;", "™");

	return text;
}

std::string extractHtmlTitleFallback(const std::string & htmlText) {
	constexpr size_t kMaxHtmlSize = 5 * 1024 * 1024;
	if (htmlText.size() > kMaxHtmlSize) {
		return {};
	}

	try {
		const std::regex titleRegex(
			"<title[^>]*>([\\s\\S]*?)</title>",
			std::regex_constants::icase);
		std::smatch match;
		if (!std::regex_search(htmlText, match, titleRegex) || match.size() < 2) {
			return {};
		}
		return normalizeWhitespace(decodeBasicHtmlEntities(match[1].str()));
	} catch (const std::regex_error &) {
		return {};
	} catch (const std::exception &) {
		return {};
	}
}

std::vector<std::string> extractHtmlLinksFallback(
	const std::string & sourceUrl,
	const std::string & htmlText) {
	constexpr size_t kMaxHtmlSize = 5 * 1024 * 1024;
	if (htmlText.size() > kMaxHtmlSize) {
		return {};
	}

	try {
		const std::regex hrefRegex(
			"<a[^>]+href\\s*=\\s*[\"']([^\"']+)[\"']",
			std::regex_constants::icase);
		std::unordered_set<std::string> seen;
		std::vector<std::string> links;
		for (std::sregex_iterator it(htmlText.begin(), htmlText.end(), hrefRegex), end;
			it != end;
			++it) {
			const std::string resolved = resolveRelativeUrl(sourceUrl, (*it)[1].str());
			if (resolved.empty() || !seen.insert(resolved).second) {
				continue;
			}
			links.push_back(resolved);
		}
		return links;
	} catch (const std::regex_error &) {
		return {};
	} catch (const std::exception &) {
		return {};
	}
}

std::string extractHtmlTextFallback(const std::string & htmlText) {
	constexpr size_t kMaxHtmlSize = 5 * 1024 * 1024;
	if (htmlText.size() > kMaxHtmlSize) {
		return {};
	}

	std::string text = htmlText;

	try {
		text = std::regex_replace(
			text,
			std::regex("<script[^>]*>[\\s\\S]*?</script>", std::regex_constants::icase),
			" ");
		text = std::regex_replace(
			text,
			std::regex("<style[^>]*>[\\s\\S]*?</style>", std::regex_constants::icase),
			" ");
		text = std::regex_replace(
			text,
			std::regex("</?(p|div|section|article|main|li|ul|ol|h1|h2|h3|h4|h5|h6|blockquote|br)[^>]*>",
				std::regex_constants::icase),
			"\n");
		text = std::regex_replace(text, std::regex("<[^>]+>"), " ");
	} catch (const std::regex_error &) {
		return {};
	} catch (const std::exception &) {
		return {};
	}

	text = decodeBasicHtmlEntities(text);

	std::istringstream stream(text);
	std::ostringstream cleaned;
	std::string line;
	bool wroteAny = false;
	while (std::getline(stream, line)) {
		const std::string normalizedLine = normalizeWhitespace(line);
		if (normalizedLine.empty()) {
			continue;
		}
		if (wroteAny) {
			cleaned << "\n\n";
		}
		cleaned << normalizedLine;
		wroteAny = true;
	}
	return trimCopy(cleaned.str());
}

std::string nodeNameLower(xmlNodePtr node) {
	if (!node || !node->name) {
		return {};
	}
	return toLowerCopy(reinterpret_cast<const char *>(node->name));
}

#if OFXGGML_HAS_LIBXML2
xmlXPathObjectPtr evaluateXPath(
	const LibXml2Api & api,
	xmlDocPtr doc,
	xmlNodePtr contextNode,
	const char * expression) {
	if (!api.available || !doc || !expression) {
		return nullptr;
	}
	xmlXPathContextPtr context = api.xmlXPathNewContextFn(doc);
	if (!context) {
		return nullptr;
	}
	xmlNodePtr fallbackNode = doc ? doc->children : nullptr;
	while (fallbackNode && fallbackNode->type != XML_ELEMENT_NODE) {
		fallbackNode = fallbackNode->next;
	}
	context->node = contextNode ? contextNode : fallbackNode;
	xmlXPathObjectPtr result = api.xmlXPathEvalExpressionFn(
		reinterpret_cast<const xmlChar *>(expression),
		context);
	api.xmlXPathFreeContextFn(context);
	return result;
}

std::string xmlNodeText(
	const LibXml2Api & api,
	xmlNodePtr node) {
	if (!api.available || !node) {
		return {};
	}
	xmlChar * rawText = api.xmlNodeGetContentFn(node);
	if (!rawText) {
		return {};
	}
	const std::string text = normalizeWhitespace(
		reinterpret_cast<const char *>(rawText));
	api.xmlFreeFn(rawText);
	return text;
}

std::string xmlNodeProperty(
	const LibXml2Api & api,
	xmlNodePtr node,
	const char * key) {
	if (!api.available || !node || !key) {
		return {};
	}
	xmlChar * rawValue = api.xmlGetPropFn(
		node,
		reinterpret_cast<const xmlChar *>(key));
	if (!rawValue) {
		return {};
	}
	const std::string value = trimCopy(reinterpret_cast<const char *>(rawValue));
	api.xmlFreeFn(rawValue);
	return value;
}

xmlNodePtr pickFirstNode(xmlXPathObjectPtr object) {
	if (!object ||
		!object->nodesetval ||
		object->nodesetval->nodeNr <= 0 ||
		!object->nodesetval->nodeTab) {
		return nullptr;
	}
	return object->nodesetval->nodeTab[0];
}

xmlNodePtr findFirstElementByTagName(
	xmlNodePtr node,
	std::initializer_list<const char *> names) {
	for (xmlNodePtr current = node; current; current = current->next) {
		if (current->type == XML_ELEMENT_NODE) {
			const std::string tag = nodeNameLower(current);
			for (const char * name : names) {
				if (tag == name) {
					return current;
				}
			}
		}
		if (current->children) {
			if (xmlNodePtr nested = findFirstElementByTagName(current->children, names)) {
				return nested;
			}
		}
	}
	return nullptr;
}

bool hasMainLikeRoleOrId(
	const LibXml2Api & api,
	xmlNodePtr node) {
	if (!node || node->type != XML_ELEMENT_NODE) {
		return false;
	}
	const std::string role = toLowerCopy(xmlNodeProperty(api, node, "role"));
	if (role == "main") {
		return true;
	}
	const std::string id = toLowerCopy(xmlNodeProperty(api, node, "id"));
	return id == "content" || id == "main" || id == "article";
}

xmlNodePtr findFirstMainLikeNode(
	const LibXml2Api & api,
	xmlNodePtr node) {
	for (xmlNodePtr current = node; current; current = current->next) {
		if (hasMainLikeRoleOrId(api, current)) {
			return current;
		}
		if (current->children) {
			if (xmlNodePtr nested = findFirstMainLikeNode(api, current->children)) {
				return nested;
			}
		}
	}
	return nullptr;
}

std::string extractLibXmlTitle(
	const LibXml2Api & api,
	xmlNodePtr node) {
	for (xmlNodePtr current = node; current; current = current->next) {
		if (current->type == XML_ELEMENT_NODE) {
			const std::string tag = nodeNameLower(current);
			if (tag == "meta") {
				const std::string property =
					toLowerCopy(xmlNodeProperty(api, current, "property"));
				const std::string name =
					toLowerCopy(xmlNodeProperty(api, current, "name"));
				if (property == "og:title" ||
					name == "twitter:title" ||
					name == "title") {
					const std::string content =
						xmlNodeProperty(api, current, "content");
					if (!content.empty()) {
						return content;
					}
				}
			}
			if (tag == "title" || tag == "h1" || tag == "h2") {
				const std::string text = xmlNodeText(api, current);
				if (!text.empty()) {
					return text;
				}
			}
		}
		if (current->children) {
			const std::string nested = extractLibXmlTitle(api, current->children);
			if (!nested.empty()) {
				return nested;
			}
		}
	}
	return {};
}

bool shouldSkipLibXmlSubtree(const std::string & tag) {
	return tag == "nav" ||
		tag == "header" ||
		tag == "footer" ||
		tag == "aside" ||
		tag == "form" ||
		tag == "script" ||
		tag == "style" ||
		tag == "noscript";
}

void appendLibXmlBlockMarkdown(
	const std::string & tag,
	const std::string & text,
	std::ostringstream & bodyMarkdown) {
	if (tag == "h1") {
		bodyMarkdown << "# " << text << "\n\n";
	} else if (tag == "h2") {
		bodyMarkdown << "## " << text << "\n\n";
	} else if (tag == "h3" || tag == "h4") {
		bodyMarkdown << "### " << text << "\n\n";
	} else if (tag == "li") {
		bodyMarkdown << "- " << text << "\n";
	} else if (tag == "blockquote") {
		bodyMarkdown << "> " << text << "\n\n";
	} else {
		bodyMarkdown << text << "\n\n";
	}
}

void collectLibXmlReadableBlocks(
	const LibXml2Api & api,
	xmlNodePtr node,
	std::unordered_set<std::string> & seenBlocks,
	std::ostringstream & bodyMarkdown) {
	for (xmlNodePtr current = node; current; current = current->next) {
		if (current->type != XML_ELEMENT_NODE) {
			if (current->children) {
				collectLibXmlReadableBlocks(
					api,
					current->children,
					seenBlocks,
					bodyMarkdown);
			}
			continue;
		}

		const std::string tag = nodeNameLower(current);
		if (shouldSkipLibXmlSubtree(tag)) {
			continue;
		}

		if (tag == "h1" ||
			tag == "h2" ||
			tag == "h3" ||
			tag == "h4" ||
			tag == "p" ||
			tag == "li" ||
			tag == "blockquote" ||
			tag == "pre") {
			const std::string text = xmlNodeText(api, current);
			if (!text.empty() && seenBlocks.insert(text).second) {
				appendLibXmlBlockMarkdown(tag, text, bodyMarkdown);
			}
		}

		if (current->children) {
			collectLibXmlReadableBlocks(
				api,
				current->children,
				seenBlocks,
				bodyMarkdown);
		}
	}
}

void collectLibXmlLinks(
	const LibXml2Api & api,
	xmlNodePtr node,
	const std::string & sourceUrl,
	std::unordered_set<std::string> & seenLinks,
	std::vector<std::string> & links) {
	for (xmlNodePtr current = node; current; current = current->next) {
		if (current->type != XML_ELEMENT_NODE) {
			if (current->children) {
				collectLibXmlLinks(
					api,
					current->children,
					sourceUrl,
					seenLinks,
					links);
			}
			continue;
		}

		const std::string tag = nodeNameLower(current);
		if (shouldSkipLibXmlSubtree(tag)) {
			continue;
		}

		if (tag == "a") {
			const std::string href = xmlNodeProperty(api, current, "href");
			const std::string resolved = resolveRelativeUrl(sourceUrl, href);
			if (!resolved.empty() && seenLinks.insert(resolved).second) {
				links.push_back(resolved);
			}
		}

		if (current->children) {
			collectLibXmlLinks(
				api,
				current->children,
				sourceUrl,
				seenLinks,
				links);
		}
	}
}
#endif

struct NativeFetchResult {
	bool success = false;
	std::string effectiveUrl;
	std::string contentType;
	std::string body;
	std::string error;
	long httpStatus = 0;
};

NativeFetchResult fetchUrlWithCurl(const std::string & url) {
	NativeFetchResult result;
	const std::string curlExe = resolveCurlExecutable();
	if (curlExe.empty()) {
		result.error = "curl executable was not found for native web crawling. Please ensure curl is installed and in your PATH, or specify the executable path in the request.";
		return result;
	}

	const std::string tempDir = makeTempOutputDir();
	if (tempDir.empty()) {
		result.error = "Could not create a temporary directory for curl output. Check disk space and permissions in your temp directory.";
		return result;
	}

	const std::filesystem::path bodyPath =
		std::filesystem::path(tempDir) / "body.bin";
	const std::filesystem::path headerPath =
		std::filesystem::path(tempDir) / "headers.txt";
	const std::string metadataMarker = "__OFXGGML_CURL_METADATA__:";
	std::vector<std::string> args = {
		curlExe,
		"--location",
		"--silent",
		"--show-error",
		"--output",
		bodyPath.string(),
		"--dump-header",
		headerPath.string(),
		"--write-out",
		metadataMarker + "%{http_code}\n" +
			metadataMarker + "%{content_type}\n" +
			metadataMarker + "%{url_effective}",
		url
	};
	std::string commandOutput;
	int exitCode = -1;
	if (!runProcessCapture(args, tempDir, &commandOutput, &exitCode) || exitCode != 0) {
		std::error_code ec;
		std::filesystem::remove_all(tempDir, ec);
		result.error = "curl request failed";
		const std::string trimmedOutput = trimCopy(commandOutput);
		if (!trimmedOutput.empty()) {
			result.error += ": " + trimmedOutput;
		}
		return result;
	}

	std::vector<std::string> metadata;
	std::istringstream outputStream(commandOutput);
	std::string line;
	while (std::getline(outputStream, line)) {
		if (line.rfind(metadataMarker, 0) == 0) {
			metadata.push_back(line.substr(metadataMarker.size()));
		}
	}
	if (!metadata.empty()) {
		try {
			result.httpStatus = std::stol(metadata[0]);
		} catch (...) {
			result.httpStatus = 0;
		}
	}
	if (metadata.size() > 1) {
		result.contentType = metadata[1];
	}
	if (metadata.size() > 2) {
		result.effectiveUrl = metadata[2];
	}
	if (result.effectiveUrl.empty()) {
		result.effectiveUrl = url;
	}
	{
		size_t bodySize = 0;
		readTextFile(bodyPath, &result.body, &bodySize);
	}
	std::error_code cleanupEc;
	std::filesystem::remove_all(tempDir, cleanupEc);

	if (!isFileUrl(result.effectiveUrl) && result.httpStatus >= 400) {
		result.error =
			"HTTP request failed with status " + std::to_string(result.httpStatus) + ".";
		return result;
	}
	if (result.body.empty()) {
		result.error = "HTTP response body was empty. The URL may not exist or the server returned no content. HTTP status: " + std::to_string(result.httpStatus);
		return result;
	}

	result.success = true;
	return result;
}

std::string normalizePathSegments(const std::string & path) {
	if (path.empty()) {
		return "/";
	}
	const bool leadingSlash = path.front() == '/';
	std::vector<std::string> segments;
	size_t start = 0;
	while (start <= path.size()) {
		const size_t slash = path.find('/', start);
		const size_t end = slash == std::string::npos ? path.size() : slash;
		const std::string part = path.substr(start, end - start);
		if (!part.empty() && part != ".") {
			if (part == "..") {
				if (!segments.empty()) {
					segments.pop_back();
				}
			} else {
				segments.push_back(part);
			}
		}
		if (slash == std::string::npos) {
			break;
		}
		start = slash + 1;
	}

	std::ostringstream joined;
	if (leadingSlash) {
		joined << '/';
	}
	for (size_t i = 0; i < segments.size(); ++i) {
		if (i > 0) {
			joined << '/';
		}
		joined << segments[i];
	}
	const std::string normalized = joined.str();
	return normalized.empty() ? (leadingSlash ? "/" : std::string()) : normalized;
}

std::string resolveRelativeUrl(
	const std::string & baseUrl,
	const std::string & href) {
	const std::string trimmedHref = trimCopy(stripUrlFragment(href));
	if (trimmedHref.empty()) {
		return {};
	}
	const std::string loweredHref = toLowerCopy(trimmedHref);
	if (loweredHref.rfind("javascript:", 0) == 0 ||
		loweredHref.rfind("mailto:", 0) == 0 ||
		loweredHref.rfind("data:", 0) == 0 ||
		loweredHref.rfind("tel:", 0) == 0) {
		return {};
	}
	const size_t schemePos = trimmedHref.find(':');
	if (schemePos != std::string::npos &&
		trimmedHref.find('/') != 0 &&
		trimmedHref.find('?') != 0 &&
		trimmedHref.find('#') != 0 &&
		trimmedHref.substr(0, schemePos).find_first_not_of(
			"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+.-") ==
			std::string::npos) {
		return trimmedHref;
	}

	const size_t baseSchemePos = baseUrl.find("://");
	if (baseSchemePos == std::string::npos) {
		return {};
	}
	const std::string scheme = baseUrl.substr(0, baseSchemePos);
	if (trimmedHref.rfind("//", 0) == 0) {
		return scheme + ":" + trimmedHref;
	}

	const size_t authorityStart = baseSchemePos + 3;
	const size_t authorityEnd = baseUrl.find_first_of("/?#", authorityStart);
	const std::string authority = authorityEnd == std::string::npos
		? baseUrl.substr(authorityStart)
		: baseUrl.substr(authorityStart, authorityEnd - authorityStart);
	const std::string rootPrefix = scheme + "://" + authority;
	if (!trimmedHref.empty() && trimmedHref.front() == '/') {
		return rootPrefix + normalizePathSegments(trimmedHref);
	}

	const std::string basePath = authorityEnd == std::string::npos
		? "/"
		: baseUrl.substr(authorityEnd, baseUrl.find_first_of("?#", authorityEnd) - authorityEnd);
	const size_t lastSlash = basePath.find_last_of('/');
	const std::string baseDir = lastSlash == std::string::npos
		? "/"
		: basePath.substr(0, lastSlash + 1);
	return rootPrefix + normalizePathSegments(baseDir + trimmedHref);
}

#if OFXGGML_HAS_LIBXML2
struct NativeParsedPage {
	bool success = false;
	std::string title;
	std::string markdown;
	std::vector<std::string> links;
	std::string error;
	std::string parserName;
};

std::string formatParsedMarkdown(
	const std::string & sourceUrl,
	const std::string & title,
	const std::string & body) {
	const std::string trimmedBody = trimCopy(body);
	if (trimmedBody.empty()) {
		return {};
	}
	if (title.empty()) {
		return trimmedBody;
	}

	std::ostringstream markdown;
	markdown << "---\n";
	markdown << "title: " << title << "\n";
	markdown << "source_url: " << sourceUrl << "\n";
	markdown << "---\n";
	markdown << "# " << title << "\n\n";
	markdown << trimmedBody;
	return trimCopy(markdown.str());
}

NativeParsedPage parseHtmlToMarkdown(
	const std::string & sourceUrl,
	const std::string & htmlText) {
	NativeParsedPage parsed;
	const NativeHtmlNormalizationResult normalized =
		normalizeHtmlWithXmllint(htmlText);
	if (normalized.success) {
		parsed.title = extractHtmlTitleFallback(normalized.normalizedHtml);
		parsed.links =
			extractHtmlLinksFallback(sourceUrl, normalized.normalizedHtml);
		parsed.markdown = formatParsedMarkdown(
			sourceUrl,
			parsed.title,
			extractHtmlTextFallback(normalized.normalizedHtml));
		parsed.success = !parsed.markdown.empty();
		if (parsed.success) {
			parsed.parserName = "libxml2 xmllint HTML";
			return parsed;
		}
		parsed.error =
			"xmllint normalized the HTML, but no readable text was extracted.";
	}

	const std::string fallbackTitle = extractHtmlTitleFallback(htmlText);
	const std::vector<std::string> fallbackLinks =
		extractHtmlLinksFallback(sourceUrl, htmlText);
	const std::string fallbackBody = extractHtmlTextFallback(htmlText);

	parsed.error = normalized.error.empty()
		? "libxml2 HTML normalization was unavailable; falling back to text extraction."
		: normalized.error;

	parsed.title = fallbackTitle;
	parsed.links = fallbackLinks;
	parsed.markdown = formatParsedMarkdown(sourceUrl, parsed.title, fallbackBody);
	parsed.success = !parsed.markdown.empty();
	if (parsed.success) {
		parsed.parserName = "HTML text fallback";
		if (!parsed.error.empty()) {
			parsed.error += " Falling back to text extraction.";
		}
		return parsed;
	}
	if (parsed.error.empty()) {
		parsed.error = "No readable text blocks were extracted from the HTML page.";
	}
	return parsed;
}
#endif

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
		const std::string trimmedOutput = trimCopy(result.commandOutput);
		if (!trimmedOutput.empty()) {
			result.error += " Output: " + trimmedOutput;
		}
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

namespace {

class ofxGgmlNativeWebCrawlerBackend final : public ofxGgmlWebCrawlerBackend {
public:
	std::string backendName() const override {
		return "NativeHtml";
	}

	ofxGgmlWebCrawlerResult crawl(
		const ofxGgmlWebCrawlerRequest & request) const override {
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

#if !OFXGGML_HAS_LIBXML2
		result.error =
			"Native HTML crawling is unavailable because libxml2 headers were not found at build time.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
#else
		if (!request.allowedDomains.empty() &&
			!isFileUrl(result.startUrl) &&
			!isUrlAllowedForDomains(result.startUrl, request.allowedDomains)) {
			result.error =
				"Crawler start URL is outside the allowedDomains restriction.";
			result.elapsedMs = elapsedMsSince(start);
			return result;
		}

		bool createdTempDir = false;
		result.outputDir = trimCopy(request.outputDir);
		const bool shouldPersistFiles =
			!result.outputDir.empty() || request.keepOutputFiles;
		if (shouldPersistFiles && result.outputDir.empty()) {
			result.outputDir = makeTempOutputDir();
			createdTempDir = true;
		}
		if (shouldPersistFiles && result.outputDir.empty()) {
			result.error = "Could not create crawler output directory.";
			result.elapsedMs = elapsedMsSince(start);
			return result;
		}
		if (!result.outputDir.empty()) {
			std::error_code ec;
			std::filesystem::create_directories(result.outputDir, ec);
			if (ec) {
				result.error =
					"Could not create crawler output directory: " + result.outputDir;
				result.elapsedMs = elapsedMsSince(start);
				return result;
			}
		}

		std::queue<std::pair<std::string, int>> pending;
		std::unordered_set<std::string> visited;
		std::string firstError;
		pending.push({result.startUrl, 0});
		const int maxDepth = std::max(0, request.maxDepth);
		const size_t maxPages = 24;

		while (!pending.empty() && visited.size() < maxPages) {
			const auto [currentUrlRaw, currentDepth] = pending.front();
			pending.pop();

			const std::string currentUrl = stripUrlFragment(currentUrlRaw);
			if (currentUrl.empty() || !visited.insert(currentUrl).second) {
				continue;
			}
			if (!request.allowedDomains.empty() &&
				!isFileUrl(currentUrl) &&
				!isUrlAllowedForDomains(currentUrl, request.allowedDomains)) {
				continue;
			}

			NativeFetchResult fetched = fetchUrlWithCurl(currentUrl);
			if (!fetched.success) {
				result.commandOutput +=
					"Native fetch failed for " + currentUrl + ": " + fetched.error + "\n";
				if (firstError.empty()) {
					firstError = "Native fetch failed for " + currentUrl + ": " + fetched.error;
				}
				continue;
			}
			if (!isFileUrl(currentUrl) && !isLikelyHtmlContentType(fetched.contentType)) {
				result.commandOutput +=
					"Skipped non-HTML content for " + currentUrl + ": " + fetched.contentType + "\n";
				if (firstError.empty()) {
					firstError =
						"Native crawler skipped non-HTML content for " + currentUrl + ".";
				}
				continue;
			}
			result.commandOutput +=
				"Fetched native page " + currentUrl +
				" (" + std::to_string(fetched.body.size()) + " bytes)\n";

			NativeParsedPage parsed = parseHtmlToMarkdown(
				fetched.effectiveUrl.empty() ? currentUrl : fetched.effectiveUrl,
				fetched.body);
			if (!parsed.success) {
				result.commandOutput +=
					"Native parse produced no content for " + currentUrl +
					": " + parsed.error + "\n";
				if (firstError.empty()) {
					firstError =
						"Native HTML parsing failed for " + currentUrl + ": " + parsed.error;
				}
				continue;
			}
			result.commandOutput +=
				"Parsed native page " + currentUrl +
				" with " + parsed.parserName;
			if (!parsed.error.empty()) {
				result.commandOutput += " (" + parsed.error + ")";
			}
			result.commandOutput += "\n";

			ofxGgmlCrawledDocument document;
			document.title = parsed.title.empty()
				? normalizedFileStemFromUrl(fetched.effectiveUrl.empty() ? currentUrl : fetched.effectiveUrl, result.documents.size())
				: parsed.title;
			document.sourceUrl = fetched.effectiveUrl.empty() ? currentUrl : fetched.effectiveUrl;
			document.markdown = std::move(parsed.markdown);
			document.crawlDepth = currentDepth;
			document.byteSize = document.markdown.size();

			if (!result.outputDir.empty()) {
				const std::filesystem::path path =
					std::filesystem::path(result.outputDir) /
					(normalizedFileStemFromUrl(document.sourceUrl, result.documents.size()) + ".md");
				std::ofstream out(path, std::ios::binary);
				if (out.is_open()) {
					out << document.markdown;
					document.localPath = path.string();
					result.savedFiles.push_back(document.localPath);
				}
			}

			result.documents.push_back(document);
			result.commandOutput += "Fetched " + document.sourceUrl + "\n";

			if (currentDepth >= maxDepth) {
				continue;
			}
			for (const auto & link : parsed.links) {
				if (link.empty() || visited.find(link) != visited.end()) {
					continue;
				}
				if (!request.allowedDomains.empty() &&
					!isFileUrl(link) &&
					!isUrlAllowedForDomains(link, request.allowedDomains)) {
					continue;
				}
				pending.push({link, currentDepth + 1});
			}
		}

		if (result.documents.empty()) {
			result.error = firstError.empty()
				? "Native HTML crawl did not produce any readable documents."
				: firstError;
			result.elapsedMs = elapsedMsSince(start);
			if (createdTempDir && !request.keepOutputFiles) {
				std::error_code ec;
				std::filesystem::remove_all(result.outputDir, ec);
				result.outputDir.clear();
			}
			return result;
		}

		result.success = true;
		result.elapsedMs = elapsedMsSince(start);
		if (createdTempDir && !request.keepOutputFiles) {
			std::error_code ec;
			std::filesystem::remove_all(result.outputDir, ec);
			result.savedFiles.clear();
			result.outputDir.clear();
			for (auto & document : result.documents) {
				document.localPath.clear();
			}
		}
		return result;
#endif
	}
};

class ofxGgmlHybridWebCrawlerBackend final : public ofxGgmlWebCrawlerBackend {
public:
	ofxGgmlHybridWebCrawlerBackend()
		: m_native(std::make_shared<ofxGgmlNativeWebCrawlerBackend>())
		, m_mojo(std::make_shared<ofxGgmlMojoWebCrawlerBackend>()) {}

	std::string backendName() const override {
		return "NativeHtml+Mojo";
	}

	ofxGgmlWebCrawlerResult crawl(
		const ofxGgmlWebCrawlerRequest & request) const override {
		const bool shouldPreferNative = !request.renderJavaScript;
		std::string nativeError;
		if (shouldPreferNative && m_native) {
			auto nativeResult = m_native->crawl(request);
			if (nativeResult.success) {
				return nativeResult;
			}
			nativeError = trimCopy(nativeResult.error);
		}

		if (!m_mojo) {
			ofxGgmlWebCrawlerResult result;
			result.backendName = backendName();
			result.startUrl = request.startUrl;
			result.error = nativeError.empty()
				? "Hybrid crawler has no backend configured."
				: nativeError;
			return result;
		}

		auto mojoResult = m_mojo->crawl(request);
		if (!nativeError.empty()) {
			if (!mojoResult.commandOutput.empty() &&
				mojoResult.commandOutput.back() != '\n') {
				mojoResult.commandOutput.push_back('\n');
			}
			mojoResult.commandOutput =
				"Native HTML path fallback reason: " + nativeError + "\n" +
				mojoResult.commandOutput;
			if (!mojoResult.success) {
				mojoResult.error =
					"Native HTML crawl failed: " + nativeError +
					" Fallback Mojo crawl failed: " + mojoResult.error;
			}
		}
		return mojoResult;
	}

private:
	std::shared_ptr<ofxGgmlWebCrawlerBackend> m_native;
	std::shared_ptr<ofxGgmlWebCrawlerBackend> m_mojo;
};

} // namespace

std::shared_ptr<ofxGgmlWebCrawlerBackend>
createWebCrawlerBridgeBackend(
	ofxGgmlWebCrawlerBridgeBackend::CrawlCallback callback) {
	return std::make_shared<ofxGgmlWebCrawlerBridgeBackend>(std::move(callback));
}

ofxGgmlWebCrawler::ofxGgmlWebCrawler()
	: m_backend(std::make_shared<ofxGgmlHybridWebCrawlerBackend>()) {}

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
