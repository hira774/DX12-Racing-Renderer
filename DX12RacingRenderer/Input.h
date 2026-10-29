#pragma once
#include <Windows.h>

#include <array>

class Input
{
public:
	/// @brief マウスの RawInput を登録する
	/// @param hwnd WM_INPUT を受け取るウィンドウのハンドル
	/// @return 成功したら true
	bool Init(HWND hwnd);

	/// @brief 今のキー状態を前のキー状態にコピーし、マウスの移動量を0に戻す
	/// @details メッセージを処理する前に実行
	void BeginFrame();

	/// @brief 押している間 true
	/// @param key 仮想キーコード
	bool IsPressed(UINT key) const;

	/// @brief 押した瞬間に true
	/// @param key 仮想キーコード
	bool IsTriggered(UINT key) const;

	/// @brief 離した瞬間に true
	/// @param key 仮想キーコード
	bool IsReleased(UINT key) const;

	/// @brief マウスの横方向の移動量 (右方向が +)
	LONG GetMouseDeltaX() const;

	/// @brief マウスの縦方向の移動量 (下方向が +)
	LONG GetMouseDeltaY() const;

	/// @brief ウィンドウメッセージを受け取って、キーとマウスの状態を書き換える
	/// @param msg メッセージの種類（WM_KEYDOWN など）
	/// @param wparam キーのメッセージでは仮想キーコード
	/// @param lparam RawInput のデータのハンドル
	void HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

private:
	// 今のキーの状態
	std::array<bool, 256> keys_{};
	// 前のキーの状態
	std::array<bool, 256> prevKeys_{};
	// このフレームのマウス移動量
	LONG mouseDeltaX_ = 0;
	LONG mouseDeltaY_ = 0;
};
