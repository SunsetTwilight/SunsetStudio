#include "common.hlsli"

struct PSIn
{
	float4 pos      : SV_Position;
	float3 worldPos : TEXCOORD0;
	float3 normal   : TEXCOORD1;
};

float4 main(PSIn i) : SV_Target
{
	const float3 n = normalize(i.normal);
	const float3 l = -gLightDir.xyz;
	const float3 v = normalize(gCameraPos.xyz - i.worldPos);
	const float3 h = normalize(l + v);

	// 半球のアンビエント + ランバート + ブリン・フォン
	const float3 sky     = float3(0.30, 0.34, 0.42);
	const float3 ground  = float3(0.12, 0.10, 0.09);
	const float3 ambient = lerp(ground, sky, n.y * 0.5 + 0.5);
	const float  diffuse = saturate(dot(n, l));
	const float  spec    = pow(saturate(dot(n, h)), 48.0) * (diffuse > 0 ? 0.35 : 0.0);

	const float3 albedo = gColor.rgb;
	const float3 color  = albedo * (ambient + diffuse * float3(1.0, 0.96, 0.9)) + spec;
	return float4(linearToSrgb(color), 1);
}
