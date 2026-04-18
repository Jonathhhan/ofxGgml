#include "ofxGgmlProcessSecurity.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace ofxGgmlProcessSecurity {

std::string getEnvVarString(const char * name) {
	if (!name || *name == '\0') return {};
#ifdef _WIN32
	char * value = nullptr;
	size_t len = 0;
	const errno_t err = _dupenv_s(&value, &len, name);
	if (err != 0 || value == nullptr || len == 0) {
		if (value != nullptr) free(value);
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

bool isValidExecutablePath(const std::string & path) {
	if (path.empty()) return false;
	if (path.find('\0') != std::string::npos) return false;

	auto containsPathSeparator = [](const std::string & value) {
		return value.find('/') != std::string::npos ||
			value.find('\\') != std::string::npos;
	};
	auto isLikelyPath = [&](const std::string & value) {
		std::filesystem::path fsPath(value);
		return fsPath.is_absolute() || fsPath.has_parent_path() ||
			containsPathSeparator(value);
	};
	auto isRegularExecutableFile = [](
		const std::filesystem::path & candidate) {
		std::error_code ec;
		if (!std::filesystem::exists(candidate, ec) || ec) return false;
		if (!std::filesystem::is_regular_file(candidate, ec) || ec) return false;
#ifndef _WIN32
		return access(candidate.c_str(), X_OK) == 0;
#else
		return true;
#endif
	};

	if (isLikelyPath(path)) {
		std::error_code ec;
		const std::filesystem::path fsPath(path);
		const std::filesystem::path canonical =
			std::filesystem::weakly_canonical(fsPath, ec);
		if (!ec && isRegularExecutableFile(canonical)) return true;
		return isRegularExecutableFile(fsPath);
	}

	for (char c : path) {
		const unsigned char uc = static_cast<unsigned char>(c);
		if (std::iscntrl(uc) || std::isspace(uc)) return false;
	}
	const std::string envPath = getEnvVarString("PATH");
	if (envPath.empty()) return false;

#ifdef _WIN32
	const char pathSep = ';';
	std::vector<std::string> executableExtensions;
	const std::string envPathext = getEnvVarString("PATHEXT");
	if (!envPathext.empty()) {
		std::istringstream extStream(envPathext);
		std::string ext;
		while (std::getline(extStream, ext, ';')) {
			if (!ext.empty()) executableExtensions.push_back(ext);
		}
	}
	if (executableExtensions.empty()) {
		executableExtensions = {".exe", ".bat", ".cmd", ".com"};
	}
#else
	const char pathSep = ':';
#endif

	std::istringstream pathEntries(envPath);
	std::string dir;
	while (std::getline(pathEntries, dir, pathSep)) {
		if (dir.empty()) continue;
		const std::filesystem::path base(dir);
		if (!std::filesystem::is_directory(base)) continue;
#ifdef _WIN32
		std::filesystem::path candidate = base / path;
		if (isRegularExecutableFile(candidate)) return true;
		for (const auto & ext : executableExtensions) {
			candidate = base / (path + ext);
			if (isRegularExecutableFile(candidate)) return true;
		}
#else
		if (isRegularExecutableFile(base / path)) return true;
#endif
	}
	return false;
}

#ifdef _WIN32
std::string quoteWindowsArg(const std::string & arg) {
	const bool needsQuotes =
		arg.find_first_of(" \t\"%^&|<>") != std::string::npos;
	if (!needsQuotes) {
		return arg;
	}
	std::string out;
	out.push_back('"');
	size_t backslashes = 0;
	for (char c : arg) {
		if (c == '\\') {
			++backslashes;
			continue;
		}
		if (c == '"') {
			out.append(backslashes * 2 + 1, '\\');
			out.push_back('"');
			backslashes = 0;
			continue;
		}
		if (backslashes > 0) {
			out.append(backslashes, '\\');
			backslashes = 0;
		}
		out.push_back(c);
	}
	if (backslashes > 0) {
		out.append(backslashes * 2, '\\');
	}
	out.push_back('"');
	return out;
}

bool isWindowsBatchScript(const std::string & path) {
	const std::string ext = path.empty()
		? std::string()
		: std::filesystem::path(path).extension().string();
	std::string lowered = ext;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
		[](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
	return lowered == ".bat" || lowered == ".cmd";
}

std::string resolveWindowsLaunchPath(const std::string & executable) {
	if (executable.empty()) {
		return {};
	}

	auto hasPathSeparator = [](const std::string & value) {
		return value.find('\\') != std::string::npos ||
			value.find('/') != std::string::npos;
	};

	const std::filesystem::path inputPath(executable);
	if (inputPath.is_absolute() || inputPath.has_parent_path() ||
		hasPathSeparator(executable)) {
		return executable;
	}

	std::vector<std::string> exts;
	const std::string pathext = getEnvVarString("PATHEXT");
	if (!pathext.empty()) {
		std::istringstream stream(pathext);
		std::string ext;
		while (std::getline(stream, ext, ';')) {
			if (!ext.empty()) {
				exts.push_back(ext);
			}
		}
	}
	if (exts.empty()) {
		exts = {".exe", ".bat", ".cmd", ".com"};
	}

	const std::string envPath = getEnvVarString("PATH");
	std::istringstream pathStream(envPath);
	std::string dir;
	while (std::getline(pathStream, dir, ';')) {
		if (dir.empty()) {
			continue;
		}

		const std::filesystem::path base(dir);
		std::error_code ec;
		if (!std::filesystem::is_directory(base, ec) || ec) {
			continue;
		}

		const std::filesystem::path direct = base / executable;
		if (std::filesystem::exists(direct, ec) && !ec) {
			return direct.string();
		}
		for (const auto & ext : exts) {
			const std::filesystem::path candidate = base / (executable + ext);
			if (std::filesystem::exists(candidate, ec) && !ec) {
				return candidate.string();
			}
		}
	}

	return executable;
}

std::string buildWindowsCommandLine(const std::vector<std::string> & args) {
	if (args.empty() || args.front().empty()) {
		return {};
	}
	const std::string resolvedExecutable = resolveWindowsLaunchPath(args.front());
	const bool useCmdWrapper = isWindowsBatchScript(resolvedExecutable);
	const std::string comspec = [&]() {
		const std::string envComspec = getEnvVarString("COMSPEC");
		return envComspec.empty()
			? std::string("C:\\Windows\\System32\\cmd.exe")
			: envComspec;
	}();

	std::string cmdLine;
	size_t cmdReserve = 0;
	for (const auto & arg : args) {
		cmdReserve += arg.size() + 3;
	}
	if (useCmdWrapper) {
		cmdReserve += resolvedExecutable.size() + comspec.size() + 32;
	}
	cmdLine.reserve(cmdReserve);
	if (useCmdWrapper) {
		cmdLine += quoteWindowsArg(comspec);
		cmdLine += " /d /s /c \"";
		cmdLine += quoteWindowsArg(resolvedExecutable);
		for (size_t i = 1; i < args.size(); ++i) {
			cmdLine.push_back(' ');
			cmdLine += quoteWindowsArg(args[i]);
		}
		cmdLine += "\"";
	} else {
		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0) cmdLine.push_back(' ');
			cmdLine += quoteWindowsArg(i == 0 ? resolvedExecutable : args[i]);
		}
	}
	return cmdLine;
}
#endif

} // namespace ofxGgmlProcessSecurity
