#pragma once

class FrameTimer
{
public:
	/// @brief コンストラクタ
	FrameTimer();

public:
	/// @brief デルタタイム(秒)を取得
	float GetDeltaTime();

private:
	/// @brief 現在のカウント数を取得 
	long long GetCounter() const;

private:
	// 1秒あたりのカウント数(Hz)
	long long frequency_;

	// 前回 GetDeltaTime() が呼ばれた時のカウント数
	long long lastCounter_;
};

