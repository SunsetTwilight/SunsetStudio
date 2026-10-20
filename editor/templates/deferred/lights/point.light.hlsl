// ポイント: 位置から全方向に。距離の 2 乗で弱まり、range で 0 になる (なめらかに切る)
LightSample Light_point(LightSurface s, LightBase b, LightParams_point p)
{
	const float3 toLight = b.position - s.position;
	const float  d2      = max(dot(toLight, toLight), 1e-4);
	const float  window  = saturate(1 - pow(d2 / (p.range * p.range), 2));
	LightSample r;
	r.direction = toLight * rsqrt(d2);
	r.radiance  = p.color * p.intensity / d2 * window * window;
	return r;
}
