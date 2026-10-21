#include "Basic.hlsli"

float4 main(VSOutput input) : SV_Target
{
	// 補間された色をそのまま返す
	return input.color;
}
