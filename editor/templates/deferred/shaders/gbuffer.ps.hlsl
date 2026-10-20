// G-Buffer: アルベドとワールドの法線を書く (マテリアルのない描画用。物体はマテリアル (shaders/materials/) で描く)
#include "common.hlsli"

struct PSIn
{
	float4 pos      : SV_Position;
	float3 worldPos : TEXCOORD0;
	float3 normal   : TEXCOORD1;
};

struct MRT
{
	float4 albedo : SV_Target0;
	float4 normal : SV_Target1;   // ワールドの法線を [0, 1] にしたもの
};

MRT main(PSIn i)
{
	MRT o;
	o.albedo = float4(gColor.rgb, 0);                          // a = 金属度
	o.normal = float4(normalize(i.normal) * 0.5 + 0.5, 0.6);   // w = 粗さ
	return o;
}
