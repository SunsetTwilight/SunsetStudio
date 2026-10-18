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
		{ "pipeline.fullscreen",  "pipeline", "全画面のパイプライン", "pipelines", ".json",    "",   "fullscreen.vs.hlsl + 選んだ PS、SRV 1 つ" },
		{ "pipeline.compute",     "pipeline", "Compute のパイプライン", "pipelines", ".json",    "",   "選んだ CS、SRV 1 つ + UAV 1 つ" },
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

bool assetTemplateText(const std::string& id, const std::string& shader, std::string& out)
{
	if (id == "shader.fullscreen.ps") return out = k_FullscreenPS, true;
	if (id == "shader.raster.vs")     return out = k_RasterVS, true;
	if (id == "shader.raster.ps")     return out = k_RasterPS, true;
	if (id == "shader.compute.cs")    return out = k_ComputeCS, true;
	if (id == "pipeline.fullscreen")  return out = fullscreenPipeline(shader.empty() ? "shaders/composite.ps.hlsl" : shader), true;
	if (id == "pipeline.compute")     return out = computePipeline(shader), true;
	return false;
}
} // namespace studio
