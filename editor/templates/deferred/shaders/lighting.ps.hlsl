// ライティング: G-Buffer と深度から HDR (リニア) の色を求める
#include "common.hlsli"

Texture2D        gAlbedo : register(t0);
Texture2D        gNormal : register(t1);
Texture2D<float> gDepth  : register(t2);

struct PSIn
{
	float4 pos : SV_Position;
	float2 uv  : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target
{
	const int3  p     = int3(i.pos.xy, 0);
	const float depth = gDepth.Load(p);
	if (depth >= 1.0)
		return float4(srgbToLinear(float3(0.075, 0.078, 0.088)), 1);   // 背景

	// G-Buffer: gbAlbedo の a = 金属度、gbNormal の w = 粗さ (標準のマテリアル。shaders/materials/standard.gbuffer.ps.hlsl)
	const float4 ga        = gAlbedo.Load(p);
	const float4 gn        = gNormal.Load(p);
	const float3 albedo    = ga.rgb;
	const float  metallic  = saturate(ga.a);
	const float  roughness = clamp(gn.w, 0.04, 1.0);
	const float3 n         = normalize(gn.xyz * 2 - 1);

	// 深度からワールド座標を戻す
	const float2 ndc   = float2(i.uv.x * 2 - 1, 1 - i.uv.y * 2);
	float4       world = mul(gInvViewProj, float4(ndc, depth, 1));
	world.xyz /= world.w;

	const float3 l = -gLightDir.xyz;
	const float3 v = normalize(gCameraPos.xyz - world.xyz);
	const float3 h = normalize(l + v);

	// 半球のアンビエント + ランバート + 正規化したブリン・フォン (粗さから鋭さ、金属度で鏡面の色)
	const float3 sky       = float3(0.30, 0.34, 0.42);
	const float3 ground    = float3(0.12, 0.10, 0.09);
	const float3 ambient   = lerp(ground, sky, n.y * 0.5 + 0.5);
	const float3 light     = float3(1.0, 0.96, 0.9);
	const float  ndl       = saturate(dot(n, l));
	const float3 diffColor = albedo * (1 - metallic);
	const float3 specColor = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
	const float  a2        = roughness * roughness;
	const float  shininess = clamp(2.0 / max(a2 * a2, 1e-4) - 2.0, 1.0, 2048.0);
	const float3 fresnel   = specColor + (1 - specColor) * pow(1 - saturate(dot(h, v)), 5);
	const float  spec      = (shininess + 8) / 25.1327 * pow(saturate(dot(n, h)), shininess);   // (n + 8) / (8π)
	return float4(diffColor * (ambient + ndl * light) + fresnel * spec * ndl * light + specColor * ambient * 0.2, 1);
}
