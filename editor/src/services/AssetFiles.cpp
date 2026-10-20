// ============================================================
//  editor/src/services/AssetFiles.cpp
// ============================================================
#include "AssetFiles.h"

#include <algorithm>
#include <cctype>

namespace studio
{
namespace fs = std::filesystem;

namespace
{
fs::path fromUtf8(const std::string& s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

std::string toUtf8(const fs::path& p)
{
	const std::u8string u = p.generic_u8string();
	return std::string(u.begin(), u.end());
}

bool endsWith(const std::string& s, const std::string& suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(c < 0x80 ? std::tolower(c) : c); });
	return s;
}
} // namespace

bool resolveAssetPath(const fs::path& root, const std::string& rel, fs::path& out, std::string& error)
{
	if (rel.empty())
		return error = "パスがありません", false;
	const fs::path r = fromUtf8(rel);
	if (r.is_absolute() || r.has_root_name() || r.has_root_directory())
		return error = "アセットのフォルダからの相対パスにしてください: " + rel, false;
	const fs::path normal = r.lexically_normal();
	if (normal.empty() || *normal.begin() == "..")
		return error = "アセットのフォルダの外は扱えません: " + rel, false;
	out = (root / normal).lexically_normal();
	return true;
}

std::string assetRelativePath(const fs::path& root, const fs::path& abs)
{
	const fs::path rel = abs.lexically_normal().lexically_relative(root.lexically_normal());
	if (rel.empty() || *rel.begin() == ".." || rel.is_absolute())
		return {};
	return toUtf8(rel);
}

std::vector<std::string> listAssetFiles(const fs::path& root, const std::string& dir, const std::vector<std::string>& extensions)
{
	std::vector<std::string> out;
	fs::path base;
	std::string error;
	if (!resolveAssetPath(root, dir.empty() ? "." : dir, base, error))
		return out;
	std::error_code ec;
	for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file(ec))
			continue;
		const std::string name = lower(toUtf8(it->path().filename()));
		const bool match = extensions.empty() || std::any_of(extensions.begin(), extensions.end(),
			[&](const std::string& e) { return endsWith(name, lower(e)); });
		if (match)
			out.push_back(assetRelativePath(root, it->path()));
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::string guessShaderStage(const std::string& path)
{
	const std::string p = lower(path);
	for (const char* s : { "vs", "ps", "cs" })
		if (p.find(std::string(".") + s + ".") != std::string::npos || endsWith(p, std::string("_") + s + ".hlsl"))
			return s;
	return {};
}

// ---- テンプレート ----
namespace
{
const char* k_FullscreenPS = R"hlsl(// 全画面のピクセルシェーダー (VS は shaders/fullscreen.vs.hlsl)
//   入力のテクスチャはパスの inputs (SRV) の順に t0, t1, ...。フレームの定数は b1 (common.hlsli)
#include "common.hlsli"

Texture2D gInput : register(t0);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	const float3 c = gInput.Load(int3(pos.xy, 0)).rgb;
	return float4(c, 1);
}
)hlsl";

const char* k_RasterVS = R"hlsl(// メッシュの頂点シェーダー (b0: オブジェクト、b1: フレーム。common.hlsli)
#include "common.hlsli"

struct VSIn
{
	float3 pos    : POSITION;
	float3 normal : NORMAL;
};
struct VSOut
{
	float4 pos    : SV_Position;
	float3 normal : NORMAL;
};

VSOut main(VSIn v)
{
	VSOut o;
	const float4 world = mul(gWorld, float4(v.pos, 1));
	o.pos    = mul(gViewProj, world);
	o.normal = normalize(mul((float3x3)gWorld, v.normal));
	return o;
}
)hlsl";

const char* k_RasterPS = R"hlsl(// メッシュのピクセルシェーダー (b0: オブジェクト、b1: フレーム。common.hlsli)
#include "common.hlsli"

struct PSIn
{
	float4 pos    : SV_Position;
	float3 normal : NORMAL;
};

float4 main(PSIn i) : SV_Target0
{
	const float ndl = saturate(dot(normalize(i.normal), -gLightDir.xyz));
	return float4(gColor.rgb * (0.2 + 0.8 * ndl), 1);
}
)hlsl";

// 描画ノード (メッシュを描く drawList のパス) の既定のシェーダー。バインドは標準のマテリアルと同じ約束:
// b0 = 物体、b1 = フレーム、b2 = cbuffer Material、t0〜 = テクスチャ、s0 = 静的サンプラー。
// マテリアルがこのレイヤーのパイプラインを持たないとき、cbuffer Material とテクスチャは名前で合わせて渡される
// 描画ノードのシェーダーは common.hlsli を include しない (空のテンプレートのプロジェクトにはない)。
// 物体とフレームの定数は、エディターが渡す並び (render/ViewportRenderer.cpp、common.hlsli と同じ) をここに書く
#define DRAW_NODE_CONSTANTS R"hlsl(
cbuffer Object : register(b0)   // 物体 (エディターが描画ごとにつなぐ)
{
	float4x4 gWorld;
	float4   gColor;       // 物体の色 (マテリアルの色に掛ける)
	uint4    gLightMask;
};

cbuffer Frame : register(b1)    // フレーム (グラフの constants の "frame")
{
	float4x4 gViewProj;
	float4x4 gInvViewProj;
	float4   gCameraPos;
	float4   gLightDir;    // xyz: 最初の平行光源の光が進む向き
	float4   gViewportSize;
};
)hlsl"

const char* k_DrawVS = R"hlsl(// 描画ノードの頂点シェーダー (b0: 物体、b1: フレーム)
)hlsl" DRAW_NODE_CONSTANTS R"hlsl(

struct VSIn
{
	float3 pos     : POSITION;
	float3 normal  : NORMAL;
	float4 tangent : TANGENT;
	float2 uv      : TEXCOORD0;
};
struct VSOut
{
	float4 pos    : SV_Position;
	float3 normal : NORMAL;
	float2 uv     : TEXCOORD0;
};

VSOut main(VSIn v)
{
	VSOut o;
	o.pos    = mul(gViewProj, mul(gWorld, float4(v.pos, 1)));
	o.normal = mul((float3x3)gWorld, v.normal);
	o.uv     = v.uv;
	return o;
}
)hlsl";

const char* k_DrawPS = R"hlsl(// 描画ノードのピクセルシェーダー: 基本色 × テクスチャ × 物体の色を、最初の平行光源の向きで簡単に照らす
//   cbuffer Material の変数とテクスチャは、マテリアルの値 (標準のマテリアルなら baseColor / alphaCutoff / baseColorMap) が
//   名前で合わせて入る。変数を足すと、マテリアルのタブとインスペクターに欄が出る
)hlsl" DRAW_NODE_CONSTANTS R"hlsl(

cbuffer Material : register(b2)
{
	float4 baseColor;
	float  metallic;      // 標準のマテリアルと並びを合わせる (使わない)
	float  roughness;
	float  alphaCutoff;   // 0 より大きければ、アルファがこれより小さい所を描かない
};

Texture2D    baseColorMap : register(t0);
SamplerState gSampler     : register(s0);

struct PSIn
{
	float4 pos    : SV_Position;
	float3 normal : NORMAL;
	float2 uv     : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target0
{
	const float4 c = baseColor * baseColorMap.Sample(gSampler, i.uv) * gColor;
	if (alphaCutoff > 0 && c.a < alphaCutoff)
		discard;
	const float ndl = saturate(dot(normalize(i.normal), -gLightDir.xyz));
	return float4(c.rgb * (0.25 + 0.75 * ndl), 1);
}
)hlsl";

const char* k_ComputeCS = R"hlsl(// コンピュートシェーダー: 入力 (t0) を読んで出力 (u0) に書く
Texture2D<float4>   gInput  : register(t0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint w, h;
	gOutput.GetDimensions(w, h);
	if (id.x >= w || id.y >= h)
		return;
	gOutput[id.xy] = gInput.Load(int3(id.xy, 0));
}
)hlsl";

std::string fullscreenPipeline(const std::string& ps)
{
	return R"({
  "version": 1,
  "type": "graphics",
  "shaders": {
    "vs": { "file": "shaders/fullscreen.vs.hlsl" },
    "ps": { "file": ")" + ps + R"(" }
  },
  "layout": {
    "parameters": [
      { "parameterType": "CBV", "shaderRegister": 1 },
      { "parameterType": "DescriptorTable", "shaderVisibility": "Pixel", "firstRange": 0, "numRanges": 1 }
    ],
    "numParameters": 2,
    "ranges": [ { "rangeType": "SRV", "numDescriptors": 1, "flags": [ "DescriptorsVolatile" ] } ],
    "numRanges": 1
  },
  "state": {
    "rasterizerState": { "cullMode": "None" },
    "depthStencilState": { "depthEnable": false, "depthWriteMask": "Zero" }
  }
}
)";
}

// depth = false: 深度を使わない (出力が $backbuffer だけのとき。アプリの出力とグラフの深度は 1 つのパスに混ぜられない)
std::string drawPipeline(const std::string& vs, const std::string& ps, bool depth)
{
	return R"({
  "version": 1,
  "type": "graphics",
  "shaders": {
    "vs": { "file": ")" + vs + R"(" },
    "ps": { "file": ")" + ps + R"(" }
  },
  "layout": {
    "parameters": [
      { "parameterType": "CBV", "shaderRegister": 0 },
      { "parameterType": "CBV", "shaderRegister": 1 },
      { "parameterType": "CBV", "shaderRegister": 2 },
      { "parameterType": "DescriptorTable", "shaderVisibility": "Pixel", "firstRange": 0, "numRanges": 1 }
    ],
    "numParameters": 4,
    "ranges": [ { "rangeType": "SRV", "numDescriptors": 1, "flags": [ "DescriptorsVolatile" ] } ],
    "numRanges": 1,
    "staticSamplers": [ { "filter": "MinMagMipLinear", "shaderVisibility": "Pixel" } ],
    "numStaticSamplers": 1,
    "flags": [ "AllowInputAssemblerInputLayout" ]
  },
  "state": {
    "rasterizerState": { "cullMode": "Back", "frontCounterClockwise": true },)" +
	       std::string(depth ? "" : R"(
    "depthStencilState": { "depthEnable": false, "depthWriteMask": "Zero" },)") + R"(
    "inputLayout": {
      "elements": [
        { "semanticName": "POSITION", "format": "R32G32B32_Float",    "alignedByteOffset": 0 },
        { "semanticName": "NORMAL",   "format": "R32G32B32_Float",    "alignedByteOffset": 12 },
        { "semanticName": "TANGENT",  "format": "R32G32B32A32_Float", "alignedByteOffset": 24 },
        { "semanticName": "TEXCOORD", "format": "R32G32_Float",       "alignedByteOffset": 40 }
      ],
      "elementCount": 4
    }
  }
}
)";
}

std::string computePipeline(const std::string& cs)
{
	return R"({
  "version": 1,
  "type": "compute",
  "shaders": {
    "cs": { "file": ")" + cs + R"(" }
  },
  "layout": {
    "parameters": [
      { "parameterType": "DescriptorTable", "firstRange": 0, "numRanges": 1 },
      { "parameterType": "DescriptorTable", "firstRange": 1, "numRanges": 1 }
    ],
    "numParameters": 2,
    "ranges": [
      { "rangeType": "SRV", "numDescriptors": 1, "flags": [ "DescriptorsVolatile" ] },
      { "rangeType": "UAV", "numDescriptors": 1, "flags": [ "DescriptorsVolatile" ] }
    ],
    "numRanges": 2
  }
}
)";
}
} // namespace

const std::vector<AssetTemplate>& assetTemplates()
{
	static const std::vector<AssetTemplate> list = {
		{ "shader.fullscreen.ps", "shader",   "全画面の PS",        "shaders",   ".ps.hlsl", "ps", "テクスチャを読んで全画面に描く (Lighting / Composite のようなパス)" },
		{ "shader.raster.vs",     "shader",   "メッシュの VS",      "shaders",   ".vs.hlsl", "vs", "頂点を変換する (GBuffer のようなパス)" },
		{ "shader.raster.ps",     "shader",   "メッシュの PS",      "shaders",   ".ps.hlsl", "ps", "メッシュの色を出す" },
		{ "shader.compute.cs",    "shader",   "Compute",            "shaders",   ".cs.hlsl", "cs", "テクスチャを読んで UAV に書く" },
		{ "shader.draw.vs",       "shader",   "描画ノードの VS",    "shaders",   ".vs.hlsl", "vs", "メッシュの頂点を変換する (位置 / 法線 / UV を渡す)" },
		{ "shader.draw.ps",       "shader",   "描画ノードの PS",    "shaders",   ".ps.hlsl", "ps", "マテリアルの基本色とテクスチャを、平行光源で簡単に照らす" },
		{ "pipeline.fullscreen",  "pipeline", "全画面のパイプライン", "pipelines", ".json",    "",   "fullscreen.vs.hlsl + 選んだ PS、SRV 1 つ" },
		{ "pipeline.compute",     "pipeline", "Compute のパイプライン", "pipelines", ".json",    "",   "選んだ CS、SRV 1 つ + UAV 1 つ" },
		{ "pipeline.draw",        "pipeline", "描画ノードのパイプライン", "pipelines", ".json",    "",   "選んだ VS / PS、物体 / フレーム / マテリアルの定数 + テクスチャ 1 つ (標準のマテリアルと同じ約束)" },
		{ "pipeline.draw.nodepth", "pipeline", "描画ノードのパイプライン (深度なし)", "pipelines", ".json", "", "上と同じで深度を使わない ($backbuffer に直接描くとき)" },
	};
	return list;
}

const AssetTemplate* findAssetTemplate(const std::string& id)
{
	for (const auto& t : assetTemplates())
		if (t.id == id)
			return &t;
	return nullptr;
}

bool assetTemplateText(const std::string& id, const std::string& shader, std::string& out, const std::string& vs)
{
	if (id == "shader.draw.vs")       return out = k_DrawVS, true;
	if (id == "shader.draw.ps")       return out = k_DrawPS, true;
	if (id == "pipeline.draw" || id == "pipeline.draw.nodepth")
		return out = drawPipeline(vs.empty() ? std::string("shaders/draw.vs.hlsl") : vs, shader.empty() ? std::string("shaders/draw.ps.hlsl") : shader,
		                          id == "pipeline.draw"), true;
	if (id == "shader.fullscreen.ps") return out = k_FullscreenPS, true;
	if (id == "shader.raster.vs")     return out = k_RasterVS, true;
	if (id == "shader.raster.ps")     return out = k_RasterPS, true;
	if (id == "shader.compute.cs")    return out = k_ComputeCS, true;
	if (id == "pipeline.fullscreen")  return out = fullscreenPipeline(shader.empty() ? "shaders/composite.ps.hlsl" : shader), true;
	if (id == "pipeline.compute")     return out = computePipeline(shader), true;
	return false;
}
} // namespace studio
