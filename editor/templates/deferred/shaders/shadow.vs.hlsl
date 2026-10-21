// 影のパス (光の視点で深さを書く) の頂点シェーダー。
// フレームの定数 (b1) の gViewProj は、パスの view "shadow" で光の視点になる
#include "common.hlsli"

float4 main(float3 pos : POSITION) : SV_Position
{
	return mul(gViewProj, mul(gWorld, float4(pos, 1)));
}
