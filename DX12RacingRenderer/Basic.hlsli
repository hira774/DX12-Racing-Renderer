// 頂点シェーダーの出力 = ピクセルシェーダーの入力
struct VSOutput
{
	float4 position : SV_Position; 	// クリップ空間の位置。ラスタライザが使う
	float3 normal : NORMAL;			// ワールド空間の法線
	float2 uv : TEXCOORD;			// UV座標
};

// 定数バッファ
cbuffer SceneConstants : register(b0)
{
	float4x4 wvp;
	float4x4 world;
	float3 lightDirection;
};
