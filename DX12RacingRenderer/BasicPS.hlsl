#include "Basic.hlsli"

// static がないと外から値を入れるための変数になる
// 環境光の明るさ
static const float kAmbient = 0.2f;

Texture2D<float4> tex : register(t0);	// テクスチャ（ルートパラメータ 1 のテーブル）
SamplerState smp : register(s0);		// 静的サンプラ

float4 main(VSOutput input) : SV_Target
{
	float4 texColor = tex.Sample(smp, input.uv);

	// 法線の長さを1にする
	float3 normal = normalize(input.normal);

	// 光源に向かう向き
	float3 light = normalize(-lightDirection);

	// ランバート
	float lambert = saturate(dot(normal, light));

	// 明るさ (環境光 + (1 - 環境光) × ランバート)
	// 明るさが 環境光 ~ 1.0 の間になる
	float brightness = kAmbient + (1.0f - kAmbient) * lambert;

	// α値には明るさをかけない
	return float4(texColor.rgb * brightness, texColor.a);
}
