// スポット: ポイントと同じ弱まり方に、向き (ローカルの -Z) のまわりの円錐を掛ける (内側の角度まで 1、外側で 0)
LightSample Light_spot(LightSurface s, LightBase b, LightParams_spot p)
{
	const float3 toLight = b.position - s.position;
	const float  d2      = max(dot(toLight, toLight), 1e-4);
	const float3 l       = toLight * rsqrt(d2);
	const float  window  = saturate(1 - pow(d2 / (p.range * p.range), 2));
	const float  cosOuter = cos(radians(max(p.outerAngle, p.innerAngle + 0.01)));
	const float  cosInner = cos(radians(p.innerAngle));
	const float  cone     = smoothstep(cosOuter, cosInner, dot(-l, b.direction));
	LightSample r;
	r.direction = l;
	r.radiance  = p.color * p.intensity / d2 * window * window * cone;
	return r;
}
