#include "Logger.h"

#include <Windows.h>
#include <iostream>

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
	// std::string を渡すので A版の関数を使用する
	OutputDebugStringA(message.c_str());
	OutputDebugStringA("\n");
#endif
	(void)message;
}
