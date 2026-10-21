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

TEST_CASE("マテリアルの移行: 影のパスを pipelines/shadow.json に替え、描画ノードは触らない")
{
	const fs::path root = freshDir("studio_test_material_migration");
	const fs::path tmpl = freshDir("studio_test_material_migration_tmpl");
	makeTemplate(tmpl);
	write(root / "graphs/viewport.json", k_Graph);
	write(root / "pipelines/gbuffer.json", k_PlainPipeline);
	write(root / "pipelines/custom.json", k_CustomPipeline);   // b2 を使う描画ノード (物体ごとのリソースで描ける)
	write(root / "shaders/common.hlsli", "MINE");   // あるファイルは足さない

	const MaterialMigrationPlan plan = planMaterialRemoval(root, "graphs/viewport.json", tmpl);
	REQUIRE(plan.needed());
	std::vector<std::string> files;
	for (const auto& c : plan.changes) files.push_back(c.file);
	CHECK(files == std::vector<std::string>{ "graphs/viewport.json", "pipelines/shadow.json", "shaders/shadow.vs.hlsl", "shaders/shadow.ps.hlsl" });
	CHECK(plan.manual.empty());

	std::vector<std::string> written;
	std::string error;
	REQUIRE(applyMaterialRemoval(root, plan, written, error));
	CHECK(written.size() == 4);
	const std::string graph = read(root / "graphs/viewport.json");
	CHECK(graph.find("pipelines/materials/standard.shadow.json") == std::string::npos);
	CHECK(graph.find("\"pipelines/shadow.json\"") != std::string::npos);
	CHECK(read(root / "pipelines/shadow.json") == "SHADOW-PIPELINE");
	CHECK(read(root / "shaders/common.hlsli") == "MINE");
	CHECK(read(root / "pipelines/custom.json") == k_CustomPipeline);

	// 移したあとは何もない
	CHECK_FALSE(planMaterialRemoval(root, "graphs/viewport.json", tmpl).needed());
}

TEST_CASE("マテリアルの移行: 今のテンプレートのプロジェクトには何もしない")
{
	const fs::path root = freshDir("studio_test_material_migration2");
	write(root / "graphs/viewport.json", R"({ "version": 1, "resources": {}, "passes": [
	  { "name": "Shadow", "type": "Raster", "pipeline": "pipelines/shadow.json", "content": { "drawList": { "layer": "Shadow" } } },
	  { "name": "Draw", "type": "Raster", "pipeline": "pipelines/draw.json", "content": { "drawList": { "layer": "Draw" } } } ] })");
	write(root / "pipelines/draw.json", drawPipelineText("shaders/draw.vs.hlsl", "shaders/draw.ps.hlsl", false));
	const MaterialMigrationPlan plan = planMaterialRemoval(root, "graphs/viewport.json", root);
	CHECK_FALSE(plan.needed());
	CHECK(plan.manual.empty());
	// 描画ノードの既定は、物体ごとのリソース (b2 の Params と t0 の baseColorMap) を持つ
	CHECK(drawPixelShader().find("cbuffer Params : register(b2)") != std::string::npos);
	CHECK(drawPixelShader().find("baseColorMap") != std::string::npos);
	CHECK(drawPipelineText("a", "b", true).find("DescriptorTable") != std::string::npos);
}
