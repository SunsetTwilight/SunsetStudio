// 平行光源: どこでも同じ向きと強さ (太陽など)。向きは物体のローカルの -Z
LightSample Light_directional(LightSurface s, LightBase b, LightParams_directional p)
{
	LightSample r;
	r.direction = -b.direction;
	r.radiance  = p.color * p.intensity;
	return r;
}
