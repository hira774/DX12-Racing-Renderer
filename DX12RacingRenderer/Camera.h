#pragma once
#include <DirectXMath.h>

#include "Input.h"

class Camera
{
public:
	/// @brief カメラの動作
	/// @param input 入力
	/// @param deltaTime 前フレームからの経過時間(秒)
	void Update(const Input& input, float deltaTime);

	/// @brief ビュー行列を作って返す
	DirectX::XMMATRIX GetViewMatrix() const;

public:
	/// @brief カメラの座標 (UI上での表示のみに使う)
	const DirectX::XMFLOAT3& GetPosition() const { return position_; }

	/// @brief カメラの移動速度を取得
	float GetMoveSpeed() const { return moveSpeed_; }
	/// @brief カメラの移動速度を設定
	/// @param speed 移動速度(単位/秒)
	void SetMoveSpeed(float speed) { moveSpeed_ = speed; }

	/// @brief マウス感度を取得
	float GetMouseSensitivity() const { return mouseSensitivity_; }
	/// @brief マウス感度を設定
	/// @param sens マウス感度(rad/カウント)
	void SetMouseSensitivity(float sens) { mouseSensitivity_ = sens; }

private:
	/// @brief ヨー・ピッチからカメラ前方のベクトルを求める
	DirectX::XMVECTOR GetForward() const;

	/// @brief ヨーからカメラ右向きのベクトルを求める
	DirectX::XMVECTOR GetRight() const;

private:
	// 座標
	DirectX::XMFLOAT3 position_ = { 0, 0, -2 };
	// ヨー角度 (rad)
	float yaw_ = 0.0f;
	// ピッチ角度 (rad)
	float pitch_ = 0.0f;
	// 移動速度 (単位/秒)
	float moveSpeed_ = 3.0f;
	// マウス感度 (rad/カウント)
	float mouseSensitivity_ = 0.003f;
};
