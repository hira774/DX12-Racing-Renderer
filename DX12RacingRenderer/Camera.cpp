#include "Camera.h"

#include <algorithm>
#include <cmath>

void Camera::Update(const Input& input, float deltaTime)
{
	// 回転
	if (input.IsPressed(VK_RBUTTON))
	{
		yaw_ += input.GetMouseDeltaX() * mouseSensitivity_;
		pitch_ -= input.GetMouseDeltaY() * mouseSensitivity_;
	}
	// ピッチを制限
	constexpr float kPitchLimit = DirectX::XMConvertToRadians(89.0f);
	pitch_ = std::clamp(pitch_, -kPitchLimit, kPitchLimit);

	// 移動
	DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&position_);
	float move = moveSpeed_ * deltaTime;
	DirectX::XMVECTOR forward = GetForward();
	DirectX::XMVECTOR right = GetRight();
	DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	if (input.IsPressed('W'))
	{
		pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(forward, move));
	}
	if (input.IsPressed('S'))
	{
		pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(forward, move));
	}
	if (input.IsPressed('D'))
	{
		pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(right, move));
	}
	if (input.IsPressed('A'))
	{
		pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(right, move));
	}
	if (input.IsPressed('E'))
	{
		pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(up, move));
	}
	if (input.IsPressed('Q'))
	{
		pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(up, move));
	}
	DirectX::XMStoreFloat3(&position_, pos);
}

DirectX::XMMATRIX Camera::GetViewMatrix() const
{
	DirectX::XMVECTOR pos = DirectX::XMVectorSet(position_.x, position_.y, position_.z, 0.0f);

	return DirectX::XMMatrixLookToLH(
		pos,
		GetForward(),
		DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
	);
}

DirectX::XMVECTOR Camera::GetForward() const
{
	DirectX::XMVECTOR forward = DirectX::XMVectorSet(
		std::cos(pitch_) * std::sin(yaw_),
		std::sin(pitch_),
		std::cos(pitch_) * std::cos(yaw_),
		0.0f
	);
	return forward;
}

DirectX::XMVECTOR Camera::GetRight() const
{
	return DirectX::XMVectorSet(std::cos(yaw_), 0.0f, -std::sin(yaw_), 0.0f);
}
