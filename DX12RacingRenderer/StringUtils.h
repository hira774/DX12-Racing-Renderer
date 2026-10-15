#pragma once
#include <Windows.h>
#include <string>

class StringUtils
{
public:
	static std::wstring Utf8ToUtf16(const std::string& str)
	{
		if (str.empty()) { return L""; }

		const int srcLength = static_cast<int>(str.size());

		// 変換後の文字数を取得
		int size = MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			str.data(),
			srcLength,
			nullptr,
			0
		);

		if (size <= 0) { return L""; }

		std::wstring wideStr(size, L'\0');
		MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			str.c_str(),
			srcLength,
			wideStr.data(),
			size
		);

		return wideStr;
	}

	static std::string Utf16ToUtf8(const std::wstring& str)
	{
		if (str.empty()) { return ""; }

		const int srcLength = static_cast<int>(str.size());

		// 変換後バイト数を取得
		int size = WideCharToMultiByte(
			CP_UTF8,
			0,
			str.c_str(),
			srcLength,
			nullptr,
			0,
			nullptr,
			nullptr
		);

		if (size <= 0) { return ""; }

		std::string utf8str(size, '\0');
		WideCharToMultiByte(
			CP_UTF8,
			0,
			str.c_str(),
			srcLength,
			utf8str.data(),
			size,
			nullptr,
			nullptr
		);

		return utf8str;
	}
};
