// ============================================================
//  標準のマテリアル (M5-4)。取り込んだモデルのスロット (割り当てていないもの) と立方体で使う。
//
//  エディターは cbuffer Material の変数とテクスチャの変数を、リフレクションでインスペクターの欄にする。
//  マテリアルのファイルの params / textures の名前は、ここの変数名と同じ。
//  バインドの約束: b0 = 物体 (common.hlsli の Object)、b1 = フレーム、b2 = マテリアル、t0〜 = テクスチャ (表)、s0 = 静的サンプラー。
//  テクスチャの色空間は変数名で決まる (Color / Albedo / Emissive を含めば sRGB)。割り当てていなければ、
//  名前に Normal を含むものは平らな法線、ほかは白。
// ============================================================
#ifndef STANDARD_MATERIAL_HLSLI
#define STANDARD_MATERIAL_HLSLI

#include "../common.hlsli"

cbuffer Material : register(b2)
{
	float4 baseColor;     // 基本色 (リニア)。基本色のテクスチャと物体の色を掛ける
	float  metallic;      // 金属度 (0〜1)。metallicRoughnessMap の B を掛ける
	float  roughness;     // 粗さ (0〜1)。metallicRoughnessMap の G を掛ける
	float  alphaCutoff;   // 0 より大きければ、アルファがこれより小さい所を描かない (切り抜き)
};

Texture2D    baseColorMap         : register(t0);
Texture2D    normalMap            : register(t1);   // 接線空間 (OpenGL の向き: Y が上)
Texture2D    metallicRoughnessMap : register(t2);   // glTF と同じ: G = 粗さ、B = 金属度
SamplerState gSampler             : register(s0);

struct VSIn
{
	float3 pos     : POSITION;
	float3 normal  : NORMAL;
	float4 tangent : TANGENT;    // w = 従法線の向き
	float2 uv      : TEXCOORD0;
};

struct VSOut
{
	float4 pos      : SV_Position;
	float3 worldPos : TEXCOORD0;
	float3 normal   : TEXCOORD1;
	float4 tangent  : TEXCOORD2;
	float2 uv       : TEXCOORD3;
};

#endif
