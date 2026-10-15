#pragma once
#include <string>

// Logger::メンバ関数名 の形で呼び出したいのでstatic
class Logger
{
public:
	/// @brief Info メッセージを出力
	/// @param message 出力するメッセージ
	static void Info(const std::string& message);

	/// @brief Warning メッセージを出力
	/// @param message 出力するメッセージ
	static void Warn(const std::string& message);

	/// @brief Error メッセージを出力
	/// @details 呼び出した側でエラー処理を行う
	/// @param message 出力するメッセージ
	static void Error(const std::string& message);

private:
	/// @brief 出力するメッセージを生成する
	/// @param level ログレベル
	/// @param message メッセージの本文
	/// @return 出力するメッセージ
	static const std::string CreateMessage(const std::string& level, const std::string& message);

	/// @brief コンソールに出力
	/// @param message 出力するメッセージ
	static void WriteToConsole(const std::string& message);

	/// @brief デバッガーに出力
	/// @param message 出力するメッセージ
	static void WriteToDebug(const std::string& message);
};

