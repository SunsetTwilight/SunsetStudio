// ============================================================
//  マテリアル (M5-4) を外したときの、既存のプロジェクトの移行の単体テスト (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/AssetFiles.h"
#include "services/MaterialMigration.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace studio;
namespace fs = std::filesystem;

namespace
{
fs::path freshDir(const char* name)
{
	const fs::path d = fs::temp_directory_path() / name;
	std::error_code ec;
	fs::remove_all(d, ec);
	fs::create_directories(d);
	return d;
}

void write(const fs::path& p, const std::string& text)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path& p)
{
	std::ifstream f(p, std::ios::binary);
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// マテリアルのころのディファードのグラフ (影はマテリアルの影のパイプライン、描画ノード Draw が 1 つ)
const char* k_Graph = R"({
  "version": 1,
  "resources": {},
  "passes": [
    { "name": "Shadow", "type": "Raster", "pipeline": "pipelines/materials/standard.shadow.json", "view": "shadow",
      "content": { "drawList": { "layer": "Shadow" } } },
    { "name": "GBuffer", "type": "Raster", "pipeline": "pipelines/gbuffer.json",
      "content": { "drawList": { "layer": "Opaque" } } },
    { "name": "Draw", "type": "Raster", "pipeline": "pipelines/draw.json",
      "content": { "drawList": { "layer": "Draw" } } },
    { "name": "Custom", "type": "Raster", "pipeline": "pipelines/custom.json",
      "content": { "drawList": { "layer": "Custom" } } }
  ]
})";

// 今の gbuffer.json と同じく、マテリアルの定数を使わないパイプライン
const char* k_PlainPipeline = R"({ "version": 1, "type": "graphics",
  "shaders": { "vs": { "file": "shaders/gbuffer.vs.hlsl" }, "ps": { "file": "shaders/gbuffer.ps.hlsl" } },
  "layout": { "parameters": [ { "parameterType": "CBV", "shaderRegister": 0 }, { "parameterType": "CBV", "shaderRegister": 1 } ], "numParameters": 2 } })";

// 自分で書き換えた (テンプレートと違う) マテリアル前提のパイプライン
const char* k_CustomPipeline = R"({ "version": 1, "type": "graphics",
  "shaders": { "vs": { "file": "shaders/custom.vs.hlsl" }, "ps": { "file": "shaders/custom.ps.hlsl" } },
  "layout": { "parameters": [ { "parameterType": "CBV", "shaderRegister": 0 }, { "parameterType": "CBV", "shaderRegister": 1 },
                              { "parameterType": "CBV", "shaderRegister": 2 } ], "numParameters": 3 } })";

void makeTemplate(const fs::path& tmpl)
{
	write(tmpl / "deferred/pipelines/shadow.json", "SHADOW-PIPELINE");
	write(tmpl / "deferred/shaders/shadow.vs.hlsl", "SHADOW-VS");
	write(tmpl / "deferred/shaders/shadow.ps.hlsl", "SHADOW-PS");
	write(tmpl / "deferred/shaders/common.hlsli", "COMMON");
}
} // namespace

TEST_CASE("マテリアルの移行: 影と、書き換えていない描画ノードを置き換え、書き換えたものは手で直すものに挙げる")
{
	const fs::path root = freshDir("studio_test_material_migration");
	const fs::path tmpl = freshDir("studio_test_material_migration_tmpl");
	makeTemplate(tmpl);
	write(root / "graphs/viewport.json", k_Graph);
	write(root / "pipelines/gbuffer.json", k_PlainPipeline);
	// 描画ノードの既定 (マテリアルのころのテンプレートのまま)。改行が CRLF でも同じとみなす
	std::string legacy = legacyDrawPipeline("shaders/draw.vs.hlsl", "shaders/draw.ps.hlsl", true);
	std::string crlf;
	for (char c : legacy) { if (c == '\n') crlf += '\r'; crlf += c; }
	write(root / "pipelines/draw.json", crlf);
	write(root / "shaders/draw.ps.hlsl", legacyDrawPixelShader());
	write(root / "pipelines/custom.json", k_CustomPipeline);
	write(root / "shaders/custom.ps.hlsl", "cbuffer Material : register(b2) { float4 baseColor; };");
	write(root / "shaders/common.hlsli", "MINE");   // あるファイルは足さない

	const MaterialMigrationPlan plan = planMaterialRemoval(root, "graphs/viewport.json", tmpl);
	REQUIRE(plan.needed());
	std::vector<std::string> files;
	for (const auto& c : plan.changes) files.push_back(c.file);
	CHECK(files == std::vector<std::string>{ "graphs/viewport.json", "pipelines/shadow.json", "shaders/shadow.vs.hlsl",
	                                         "shaders/shadow.ps.hlsl", "pipelines/draw.json", "shaders/draw.ps.hlsl" });
	REQUIRE(plan.manual.size() == 2);
	CHECK(plan.manual[0].find("pipelines/custom.json") == 0);
	CHECK(plan.manual[1].find("shaders/custom.ps.hlsl") == 0);

	std::vector<std::string> written;
	std::string error;
	REQUIRE(applyMaterialRemoval(root, plan, written, error));
	CHECK(written.size() == 6);
	const std::string graph = read(root / "graphs/viewport.json");
	CHECK(graph.find("pipelines/materials/standard.shadow.json") == std::string::npos);
	CHECK(graph.find("\"pipelines/shadow.json\"") != std::string::npos);
	CHECK(read(root / "pipelines/shadow.json") == "SHADOW-PIPELINE");
	CHECK(read(root / "shaders/common.hlsli") == "MINE");
	CHECK(read(root / "pipelines/draw.json") == drawPipelineText("shaders/draw.vs.hlsl", "shaders/draw.ps.hlsl", true));
	CHECK(read(root / "shaders/draw.ps.hlsl") == drawPixelShader());
	CHECK(read(root / "pipelines/custom.json") == k_CustomPipeline);

	// 移したあとは、置き換えるものはない (手で直すものだけが残る)
	const MaterialMigrationPlan again = planMaterialRemoval(root, "graphs/viewport.json", tmpl);
	CHECK_FALSE(again.needed());
	CHECK(again.manual.size() == 2);
}

TEST_CASE("マテリアルの移行: 今のテンプレートのプロジェクトには何もしない")
{
	const fs::path root = freshDir("studio_test_material_migration2");
	write(root / "graphs/viewport.json", R"({ "version": 1, "resources": {}, "passes": [
	  { "name": "Shadow", "type": "Raster", "pipeline": "pipelines/shadow.json", "content": { "drawList": { "layer": "Shadow" } } },
	  { "name": "Draw", "type": "Raster", "pipeline": "pipelines/draw.json", "content": { "drawList": { "layer": "Draw" } } } ] })");
	write(root / "pipelines/draw.json", drawPipelineText("shaders/draw.vs.hlsl", "shaders/draw.ps.hlsl", false));
	write(root / "shaders/draw.ps.hlsl", drawPixelShader());
	const MaterialMigrationPlan plan = planMaterialRemoval(root, "graphs/viewport.json", root);
	CHECK_FALSE(plan.needed());
	CHECK(plan.manual.empty());
	// 描画ノードの既定には、マテリアルの定数もテクスチャもない
	CHECK(drawPixelShader().find("register(b2)") == std::string::npos);
	CHECK(drawPipelineText("a", "b", true).find("DescriptorTable") == std::string::npos);
}
