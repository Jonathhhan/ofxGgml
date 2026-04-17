#pragma once

#include <filesystem>
#include <string>

namespace ofFilePath {

inline std::string getCurrentExeDir() {
	return std::filesystem::current_path().string();
}

} // namespace ofFilePath
