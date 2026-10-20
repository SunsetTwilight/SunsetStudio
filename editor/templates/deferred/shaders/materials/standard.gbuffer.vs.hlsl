// 標準のマテリアル: G-Buffer のパス (マテリアルパス "Opaque") の頂点シェーダー
#include "standard.hlsli"

VSOut main(VSIn i)
{
	VSOut o;
	const float4 wp = mul(gWorld, float4(i.pos, 1));
	o.worldPos = wp.xyz;
	o.pos      = mul(gViewProj, wp);
	o.normal   = mul((float3x3)gWorld, i.normal);
	o.tangent  = float4(mul((float3x3)gWorld, i.tangent.xyz), i.tangent.w);
	o.uv       = i.uv;
	return o;
}
