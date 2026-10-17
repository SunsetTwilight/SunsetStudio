#include "common.hlsli"

struct PSIn
{
	float4 pos      : SV_Position;
	float3 worldPos : TEXCOORD0;
};

// 間隔 spacing の線の濃さ (画面上で 1 ピクセルほどの太さ、アンチエイリアス付き)
float gridLine(float2 p, float spacing)
{
	const float2 coord = p / spacing;
	const float2 d     = abs(frac(coord - 0.5) - 0.5) / fwidth(coord);
	return 1.0 - saturate(min(d.x, d.y));
}

float4 main(PSIn i) : SV_Target
{
	const float2 p = i.worldPos.xz;

	float  alpha = max(gridLine(p, 1.0) * 0.35, gridLine(p, 10.0) * 0.7);
	float3 color = float3(0.55, 0.57, 0.62);

	// X 軸 (赤) と Z 軸 (青)
	const float2 axisWidth = fwidth(p) * 1.5;
	if (abs(p.y) < axisWidth.y) { color = float3(0.85, 0.30, 0.30); alpha = 0.9; }
	if (abs(p.x) < axisWidth.x) { color = float3(0.30, 0.45, 0.90); alpha = 0.9; }

	// 遠くは薄くする
	const float dist = length(gCameraPos.xyz - i.worldPos);
	alpha *= saturate(1.0 - dist / 150.0);
	if (alpha <= 0.002)
		discard;
	return float4(linearToSrgb(color), alpha);
}
