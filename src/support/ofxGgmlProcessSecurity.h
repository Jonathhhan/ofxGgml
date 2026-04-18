#pragma once

#include <string>
#include <vector>

namespace ofxGgmlProcessSecurity {

std::string getEnvVarString(const char * name);
bool isValidExecutablePath(const std::string & path);

#ifdef _WIN32
std::string quoteWindowsArg(const std::string & arg);
bool isWindowsBatchScript(const std::string & path);
std::string resolveWindowsLaunchPath(const std::string & executable);
std::string buildWindowsCommandLine(const std::vector<std::string> & args);
#endif

} // namespace ofxGgmlProcessSecurity
