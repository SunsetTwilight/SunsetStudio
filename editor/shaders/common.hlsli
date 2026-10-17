// ============================================================
//  ビューポートのシェーダーで共通の定数 (editor/src/render/SceneRenderer.cpp と合わせる)
//    b0: フレームの定数 (ルート CBV)
//    b1: オブジェクトの定数 (ルート定数 20 個)
//  行列は列優先 (C++ の配列 m[列 * 4 + 行])。mul(M, v) で変換する
// ============================================================
cbuffer Frame : register(b0)
{
	float4x4 gViewProj;
	float4   gCameraPos;     // xyz
	float4   gLightDir;      // xyz: 光が進む向き (正規化済み)
	float4   gViewportSize;  // xy: ピクセル数, zw: 1 / ピクセル数
};

cbuffer Object : register(b1)
{
	float4x4 gWorld;
	float4   gColor;
};

// リニア → sRGB (バックバッファは UNORM なので、ここでガンマをかける)
float3 linearToSrgb(float3 c)
{
	return pow(saturate(c), 1.0 / 2.2);
}
