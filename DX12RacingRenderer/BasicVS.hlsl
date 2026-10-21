#include "Basic.hlsli"

// 頂点バッファから IA が読んだ1頂点
struct VSInput
{
	float3 position : POSITION; // 入力レイアウトの "POSITION" と対応
	float4 color : COLOR;       // 入力レイアウトの "COLOR" と対応
};

VSOutput main(VSInput input)
{
	VSOutput output;
	// 位置は NDC のまま。w = 1 なので、クリップ空間 = NDC
	output.position = float4(input.position, 1.0f);
	// 色はそのまま渡す
	output.color = input.color;
	return output;
}
