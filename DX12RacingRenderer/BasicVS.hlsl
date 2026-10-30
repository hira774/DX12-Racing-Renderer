#include "Basic.hlsli"

// 頂点バッファから IA が読んだ1頂点
struct VSInput
{
	float3 position : POSITION; // 入力レイアウトの "POSITION" と対応
	float3 normal : NORMAL;		// 入力レイアウトの "NORMAL" と対応
	float2 uv : TEXCOORD;		// 入力レイアウトの "TEXCOORD" と対応
};

VSOutput main(VSInput input)
{
	VSOutput output;
	// ローカル座標 (w = 1.0f として考える)に WVP行列をかけてクリップ空間の座標にする
	output.position = mul(float4(input.position, 1.0f), wvp);
	// 法線をワールド空間にする
	// w は 0にする。取り出すのは x,y,z の3つだけ
	output.normal = mul(float4(input.normal, 0.0f), world).xyz;

	output.uv = input.uv;
	return output;
}
