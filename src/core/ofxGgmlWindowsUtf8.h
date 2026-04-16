#pragma once

#ifdef _WIN32

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

inline std::wstring ofxGgmlWideFromUtf8(const std::string & text) {
	if (text.empty()) {
		return {};
	}

	int wideSize = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		text.data(),
		static_cast<int>(text.size()),
		nullptr,
		0);
	UINT codePage = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;
	if (wideSize <= 0) {
		codePage = CP_ACP;
		flags = 0;
		wideSize = MultiByteToWideChar(
			codePage,
			flags,
			text.data(),
			static_cast<int>(text.size()),
			nullptr,
			0);
		if (wideSize <= 0) {
			return std::wstring(text.begin(), text.end());
		}
	}

	std::wstring wide(static_cast<size_t>(wideSize), L'\0');
	const int converted = MultiByteToWideChar(
		codePage,
		flags,
		text.data(),
		static_cast<int>(text.size()),
		wide.data(),
		wideSize);
	if (converted <= 0) {
		return std::wstring(text.begin(), text.end());
	}
	return wide;
}

#endif
