#include "Logger.h"

#include <Windows.h>
#include <iostream>

#include "StringUtils.h"

void Logger::Info(const std::string& message)
{
	std::string msg = CreateMessage("Info", message);
	WriteToConsole(msg);
	WriteToDebug(msg);
}

void Logger::Warn(const std::string& message)
{
	std::string msg = CreateMessage("Warn", message);
	WriteToConsole(msg);
	WriteToDebug(msg);
}

void Logger::Error(const std::string& message)
{
	std::string msg = CreateMessage("Error", message);
	WriteToConsole(msg);
	WriteToDebug(msg);
}

const std::string Logger::CreateMessage(
	const std::string& level,
	const std::string& message)
{
	return level + " : " + message;
}

void Logger::WriteToConsole(const std::string& message)
{
#ifdef _DEBUG
	std::cout << message << std::endl;
#endif
	(void)message;
}

void Logger::WriteToDebug(const std::string& message)
{
#ifdef _DEBUG
	// 日本語の文字化けを防ぐために W版を使用する
	// A版だと Shift_JIS として扱われる
	OutputDebugStringW(StringUtils::Utf8ToUtf16(message).c_str());
	OutputDebugStringW(L"\n");
#endif
	(void)message;
}
