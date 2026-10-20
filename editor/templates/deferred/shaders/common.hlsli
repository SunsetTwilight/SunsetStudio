// ============================================================
//  ビューポートのシェーダーで共通の定数 (editor/src/render/ViewportRenderer.cpp と合わせる)
//    b0: オブジェクトの定数 (GBuffer のパス。アプリがオブジェクトごとにつなぐ)
//    b1: フレームの定数 (グラフの constants の "frame")
//  行列は列優先 (C++ の配列 m[列 * 4 + 行])。mul(M, v) で変換する
// ============================================================
#ifndef VIEWPORT_COMMON_HLSLI
#define VIEWPORT_COMMON_HLSLI

cbuffer Object : register(b0)
{
	float4x4 gWorld;
	float4   gColor;
	uint4    gLightMask;   // x = ライトリンクのマスク (G-Buffer の gbLightMask に書く。M5-5)
};

cbuffer Frame : register(b1)
{
	float4x4 gViewProj;
	float4x4 gInvViewProj;
	float4   gCameraPos;     // xyz
	float4   gLightDir;      // xyz: 光が進む向き (正規化済み)
	float4   gViewportSize;  // xy: ピクセル数, zw: 1 / ピクセル数
};

// リニア → sRGB (バックバッファは UNORM なので、合成でガンマをかける)
float3 linearToSrgb(float3 c)
{
	return pow(saturate(c), 1.0 / 2.2);
}
float3 srgbToLinear(float3 c)
{
	return pow(saturate(c), 2.2);
}

#endif
