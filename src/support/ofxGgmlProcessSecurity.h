#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ofxGgmlProcessSecurity {

std::string getEnvVarString(const char * name);
bool isValidExecutablePath(const std::string & path);

/// Execute a command and capture its output.
///
/// @param args Command and arguments (args[0] is the executable)
/// @param output Captured standard output (and optionally stderr)
/// @param exitCode Exit code of the process
/// @param mergeStderr If true, merge stderr into stdout
/// @param onChunk Optional callback for streaming output (line-by-line)
/// @return true if the command was executed, false on failure
bool runCommandCapture(
	const std::vector<std::string> & args,
	std::string & output,
	int & exitCode,
	bool mergeStderr = true,
	std::function<bool(const std::string &)> onChunk = nullptr);

#ifdef _WIN32
std::string quoteWindowsArg(const std::string & arg);
bool isWindowsBatchScript(const std::string & path);
std::string resolveWindowsLaunchPath(const std::string & executable);
std::string buildWindowsCommandLine(const std::vector<std::string> & args);
#endif

} // namespace ofxGgmlProcessSecurity
