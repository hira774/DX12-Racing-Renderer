#include "Input.h"

#include <hidusage.h>

#include "Logger.h"

bool Input::Init(HWND hwnd)
{
	// マウスの移動量を WM_INPUT で受け取るように登録する
	RAWINPUTDEVICE device = {};
	device.usUsagePage = HID_USAGE_PAGE_GENERIC;	// 汎用のデスクトップ機器
	device.usUsage = HID_USAGE_GENERIC_MOUSE;		// マウス
	device.dwFlags = 0;	// ウィンドウが前面にある間だけ
	device.hwndTarget = hwnd;
	if (!RegisterRawInputDevices(&device, 1, sizeof(RAWINPUTDEVICE)))
	{
		Logger::Error("Failed to register the raw input device.");
		return false;
	}

	return true;
}

void Input::BeginFrame()
{
	prevKeys_ = keys_;

	// マウスの移動量を0にする
	mouseDeltaX_ = 0;
	mouseDeltaY_ = 0;
}

bool Input::IsPressed(UINT key) const
{
	// 未定義動作を防ぐために範囲内か確認する
	if (key >= keys_.size()) { return false; }
	return keys_[key];
}

bool Input::IsTriggered(UINT key) const
{
	if (key >= keys_.size()) { return false; }
	return keys_[key] && !prevKeys_[key];
}

bool Input::IsReleased(UINT key) const
{
	if (key >= keys_.size()) { return false; }
	return !keys_[key] && prevKeys_[key];
}

LONG Input::GetMouseDeltaX() const
{
	return mouseDeltaX_;
}

LONG Input::GetMouseDeltaY() const
{
	return mouseDeltaY_;
}

void Input::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		// キーが押された
	case WM_KEYDOWN:

		if (wparam < keys_.size())
		{
			keys_[wparam] = true;
		}
		break;

		// キーが離された
	case WM_KEYUP:

		if (wparam < keys_.size())
		{
			keys_[wparam] = false;
		}
		break;

		// キーの入力をリセット
	case WM_KILLFOCUS:

		keys_.fill(false);
		break;

		// マウスの移動
	case WM_INPUT:
	{
		RAWINPUT raw = {};
		UINT size = sizeof(RAWINPUT);
		if (GetRawInputData(
			reinterpret_cast<HRAWINPUT>(lparam),
			RID_INPUT,
			&raw,
			&size,
			sizeof(RAWINPUTHEADER))
		== static_cast<UINT>(-1))
		{
			break;
		}

		// 絶対位置のときは使わない
		if (raw.header.dwType == RIM_TYPEMOUSE
			&& (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
		{
			// 移動量を足す
			// 1フレームの間に何度も足される
			mouseDeltaX_ += raw.data.mouse.lLastX;
			mouseDeltaY_ += raw.data.mouse.lLastY;
		}
		break;
	}

	// マウスのボタンはキーと同じ配列で扱う
	case WM_RBUTTONDOWN:

		keys_[VK_RBUTTON] = true;
		break;

	case WM_RBUTTONUP:

		keys_[VK_RBUTTON] = false;
		break;

		// キャプチャを失ったら、右ボタンを離した扱いにする
	case WM_CAPTURECHANGED:

		keys_[VK_RBUTTON] = false;
		break;

	default:
		break;
	}
}
