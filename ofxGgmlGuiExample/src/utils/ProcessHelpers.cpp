#include "ProcessHelpers.h"
#include "ImGuiHelpers.h"

#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <tlhelp32.h>

std::string normalizeExecutablePathForCompare(const std::string & path) {
	if (trim(path).empty()) {
		return {};
	}
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
	if (ec) {
		normalized = std::filesystem::absolute(std::filesystem::path(path), ec);
		if (ec) {
			normalized = std::filesystem::path(path);
		}
	}
	std::string value = normalized.string();
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string utf8FromWide(const std::wstring & text) {
	if (text.empty()) {
		return {};
	}

	const int utf8Size = WideCharToMultiByte(
		CP_UTF8,
		0,
		text.data(),
		static_cast<int>(text.size()),
		nullptr,
		0,
		nullptr,
		nullptr);
	if (utf8Size <= 0) {
		return std::string(text.begin(), text.end());
	}

	std::string utf8(static_cast<size_t>(utf8Size), '\0');
	const int converted = WideCharToMultiByte(
		CP_UTF8,
		0,
		text.data(),
		static_cast<int>(text.size()),
		utf8.data(),
		utf8Size,
		nullptr,
		nullptr);
	if (converted <= 0) {
		return std::string(text.begin(), text.end());
	}
	return utf8;
}

bool terminateAddonLlamaServerProcesses(const std::string & serverExePath) {
	const std::string normalizedTarget = normalizeExecutablePathForCompare(serverExePath);
	if (normalizedTarget.empty()) {
		return false;
	}

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	bool terminatedAny = false;
	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, L"llama-server.exe") != 0) {
				continue;
			}

			HANDLE processHandle = OpenProcess(
				PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
				FALSE,
				entry.th32ProcessID);
			if (!processHandle) {
				continue;
			}

			std::wstring imagePath(32768, L'\0');
			DWORD imagePathSize = static_cast<DWORD>(imagePath.size());
			if (QueryFullProcessImageNameW(processHandle, 0, imagePath.data(), &imagePathSize)) {
				imagePath.resize(imagePathSize);
				const std::string normalizedImagePath =
					normalizeExecutablePathForCompare(utf8FromWide(imagePath));
				if (normalizedImagePath == normalizedTarget) {
					if (TerminateProcess(processHandle, 0)) {
						WaitForSingleObject(processHandle, 5000);
						terminatedAny = true;
					}
				}
			}
			CloseHandle(processHandle);
		} while (Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return terminatedAny;
}

#else

bool terminateAddonLlamaServerProcesses(const std::string & serverExePath) {
	(void) serverExePath;
	return false;
}

#endif

std::string probeServerExecutable(
	const std::vector<std::filesystem::path> & candidates) {
	for (const auto & candidate : candidates) {
		std::error_code ec;
		const auto normalized = std::filesystem::weakly_canonical(candidate, ec);
		const auto & checkPath = ec ? candidate : normalized;
		if (std::filesystem::exists(checkPath, ec) && !ec) {
			return checkPath.string();
		}
	}
	return {};
}
