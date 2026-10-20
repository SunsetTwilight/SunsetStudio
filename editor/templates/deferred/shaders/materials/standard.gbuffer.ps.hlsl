// 標準のマテリアル: G-Buffer に書く
//   gbAlbedo: rgb = 基本色、a = 金属度 / gbNormal: xyz = ワールドの法線 ([0, 1] にしたもの)、w = 粗さ
//   gbLightMask: 物体のライトリンクのマスク (M5-5)
#include "standard.hlsli"

struct MRT
{
	float4 albedo : SV_Target0;
	float4 normal : SV_Target1;
	uint   mask   : SV_Target2;
};

MRT main(VSOut i)
{
	const float4 base = baseColor * baseColorMap.Sample(gSampler, i.uv) * gColor;
	if (alphaCutoff > 0 && base.a < alphaCutoff)
		discard;

	// 法線マップ (接線空間 → ワールド)
	float3       n = normalize(i.normal);
	const float3 t = normalize(i.tangent.xyz - n * dot(n, i.tangent.xyz));
	const float3 b = cross(n, t) * (i.tangent.w < 0 ? -1.0 : 1.0);
	const float3 m = normalMap.Sample(gSampler, i.uv).xyz * 2 - 1;
	n = normalize(m.x * t + m.y * b + m.z * n);

	const float4 mr = metallicRoughnessMap.Sample(gSampler, i.uv);
	MRT o;
	o.albedo = float4(base.rgb, saturate(metallic * mr.b));
	o.normal = float4(n * 0.5 + 0.5, saturate(roughness * mr.g));
	o.mask   = gLightMask.x;
	return o;
}
