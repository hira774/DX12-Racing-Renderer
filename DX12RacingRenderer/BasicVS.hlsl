#include "Basic.hlsli"

// WVP行列
cbuffer SceneConstants : register(b0)
{
	float4x4 wvp;
};

// 頂点バッファから IA が読んだ1頂点
struct VSInput
{
	float3 position : POSITION; // 入力レイアウトの "POSITION" と対応
	float4 color : COLOR;       // 入力レイアウトの "COLOR" と対応
};

VSOutput main(VSInput input)
{
	VSOutput output;
	// ローカル座標 (w = 1.0f として考える)に WVP行列をかけてクリップ空間の座標にする
	output.position = mul(float4(input.position, 1.0f), wvp);
	// 色はそのまま渡す
	output.color = input.color;
	return output;
}
