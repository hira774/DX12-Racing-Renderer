// 頂点シェーダーの出力 = ピクセルシェーダーの入力
struct VSOutput
{
	float4 position : SV_Position; // クリップ空間の位置。ラスタライザが使う
	float4 color : COLOR;          // 頂点の色。三角形の中で補間されてピクセルシェーダーに届く
};
