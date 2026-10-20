// 標準のマテリアル: 影のパス。切り抜き (alphaCutoff) のときだけ、アルファで捨てる
#include "standard.shadow.hlsli"

void main(VSOut i)
{
	if (alphaCutoff > 0 && (baseColor.a * baseColorMap.Sample(gSampler, i.uv).a * gColor.a) < alphaCutoff)
		discard;
}
