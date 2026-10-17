// 合成: HDR (リニア) の色をバックバッファ (UNORM) に書く
#include "common.hlsli"

Texture2D gScene : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target
{
	return float4(linearToSrgb(gScene.Load(int3(pos.xy, 0)).rgb), 1);
}
