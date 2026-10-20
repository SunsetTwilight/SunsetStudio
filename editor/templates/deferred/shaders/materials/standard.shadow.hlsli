// ============================================================
//  標準のマテリアルの影のパス (マテリアルパス "Shadow")。深さだけを書く。
//  フレームの定数 (b1) の gViewProj は、パスの view "shadow" で光の視点になる。
//  基本色のアルファで切り抜く (alphaCutoff > 0 のとき)。cbuffer Material は G-Buffer のパスと同じ並び
// ============================================================
#ifndef STANDARD_SHADOW_HLSLI
#define STANDARD_SHADOW_HLSLI

#include "../common.hlsli"

cbuffer Material : register(b2)
{
	float4 baseColor;
	float  metallic;
	float  roughness;
	float  alphaCutoff;
};

Texture2D    baseColorMap : register(t0);
SamplerState gSampler     : register(s0);

struct VSIn
{
	float3 pos     : POSITION;
	float3 normal  : NORMAL;
	float4 tangent : TANGENT;
	float2 uv      : TEXCOORD0;
};

struct VSOut
{
	float4 pos : SV_Position;
	float2 uv  : TEXCOORD0;
};

#endif
