#include "common.hlsli"

// y = 0 の大きな四角形 (頂点バッファなし。6 頂点の三角形リスト)
static const float k_Extent = 400.0;

struct VSOut
{
	float4 pos      : SV_Position;
	float3 worldPos : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID)
{
	static const float2 corners[6] = {
		float2(-1, -1), float2(-1, 1), float2(1, 1),
		float2(-1, -1), float2(1, 1),  float2(1, -1),
	};
	const float2 c = corners[id] * k_Extent;
	VSOut o;
	o.worldPos = float3(c.x, 0, c.y);
	o.pos      = mul(gViewProj, float4(o.worldPos, 1));
	return o;
}
