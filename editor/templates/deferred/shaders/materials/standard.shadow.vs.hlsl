// 標準のマテリアル: 影のパス (光の視点で深さを書く) の頂点シェーダー
#include "standard.shadow.hlsli"

VSOut main(VSIn i)
{
	VSOut o;
	o.pos = mul(gViewProj, mul(gWorld, float4(i.pos, 1)));
	o.uv  = i.uv;
	return o;
}
