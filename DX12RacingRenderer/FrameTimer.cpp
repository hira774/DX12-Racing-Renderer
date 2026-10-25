#include "FrameTimer.h"

#include <Windows.h>

#include "Logger.h"

FrameTimer::FrameTimer() :
	frequency_(0),
	lastCounter_(0)
{
	LARGE_INTEGER freq = {};

	if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0)
	{
		// 念のため
		Logger::Error("Failed to get frequency.");

		frequency_ = 1;
		lastCounter_ = 0;
		return;
	}
	frequency_ = freq.QuadPart;

	// 基準の時刻
	lastCounter_ = GetCounter();
}

float FrameTimer::GetDeltaTime()
{
	long long now = GetCounter();
	long long deltaCount = now - lastCounter_;
	lastCounter_ = now;

	// カウント数を秒数に変換
	return static_cast<float>(deltaCount) / static_cast<float>(frequency_);
}

long long FrameTimer::GetCounter() const
{
	LARGE_INTEGER counter = {};
	QueryPerformanceCounter(&counter);

	return counter.QuadPart;
}
