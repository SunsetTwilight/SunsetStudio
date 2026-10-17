// G-Buffer: アルベドとワールドの法線を書く
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
	o.albedo = float4(gColor.rgb, 1);
	o.normal = float4(normalize(i.normal) * 0.5 + 0.5, 1);
	return o;
}
