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

	const float3 albedo = gAlbedo.Load(p).rgb;
	const float3 n      = normalize(gNormal.Load(p).xyz * 2 - 1);

	// 深度からワールド座標を戻す
	const float2 ndc   = float2(i.uv.x * 2 - 1, 1 - i.uv.y * 2);
	float4       world = mul(gInvViewProj, float4(ndc, depth, 1));
	world.xyz /= world.w;

	const float3 l = -gLightDir.xyz;
	const float3 v = normalize(gCameraPos.xyz - world.xyz);
	const float3 h = normalize(l + v);

	// 半球のアンビエント + ランバート + ブリン・フォン
	const float3 sky     = float3(0.30, 0.34, 0.42);
	const float3 ground  = float3(0.12, 0.10, 0.09);
	const float3 ambient = lerp(ground, sky, n.y * 0.5 + 0.5);
	const float  diffuse = saturate(dot(n, l));
	const float  spec    = pow(saturate(dot(n, h)), 48.0) * (diffuse > 0 ? 0.35 : 0.0);
	return float4(albedo * (ambient + diffuse * float3(1.0, 0.96, 0.9)) + spec, 1);
}
