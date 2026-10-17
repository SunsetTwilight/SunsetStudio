#include "common.hlsli"

struct VSIn
{
	float3 pos    : POSITION;
	float3 normal : NORMAL;
};

struct VSOut
{
	float4 pos      : SV_Position;
	float3 worldPos : TEXCOORD0;
	float3 normal   : TEXCOORD1;
};

VSOut main(VSIn i)
{
	VSOut o;
	const float4 wp = mul(gWorld, float4(i.pos, 1));
	o.worldPos = wp.xyz;
	o.pos      = mul(gViewProj, wp);
	o.normal   = mul((float3x3)gWorld, i.normal);
	return o;
}
