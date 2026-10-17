// ============================================================
//  スキーマと Bridge の JSON の橋渡し (インスペクター用) の単体テスト。GPU も webview も使わない
// ============================================================
#include <doctest/doctest.h>
#include "services/SchemaJson.h"

#include <cstring>
#include <string>

using studio::Json;

TEST_CASE("describeType: 型の説明と、入れ子の構造体 / 列挙型をまとめて返す")
{
	const Json d = studio::describeType("GfxGraphicsPipelineDesc");
	REQUIRE(d.is_object());
	CHECK(d["type"] == "GfxGraphicsPipelineDesc");
	CHECK(d["structs"].contains("GfxGraphicsPipelineDesc"));
	CHECK(d["structs"].contains("GfxRasterizerDesc"));
	CHECK(d["structs"].contains("GfxRenderTargetBlendDesc"));   // 配列の要素の構造体も
	CHECK(d["enums"].contains("GfxCullMode"));
	CHECK(d["enums"].contains("GfxColorWriteMask"));
	CHECK(d["enums"]["GfxColorWriteMask"]["flags"] == true);

	bool foundCull = false;
	for (const auto& f : d["structs"]["GfxRasterizerDesc"]["fields"])
		if (f["name"] == "cullMode")
		{
			foundCull = true;
			CHECK(f["type"] == "enum");
			CHECK(f["enum"] == "GfxCullMode");
			CHECK(f["default"] == "Back");
		}
	CHECK(foundCull);

	CHECK(studio::describeType("NoSuchType").is_null());
	const Json types = studio::listTypes();
	CHECK(types["structs"].size() > 20);
	CHECK(types["enums"].size() > 20);
}

TEST_CASE("structToJson / applyJson: 記述子を JSON にして、変えたフィールドだけを書き戻す")
{
	GfxGraphicsPipelineDesc desc;
	desc.vs.id = 11;   // ハンドルは JSON に出さず、書き戻しでも変えない
	desc.ps.id = 12;
	desc.debugName = "Pipe";
	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = GfxFormat::B8G8R8A8_Unorm;

	Json j = studio::structToJson(gfxStructInfo<GfxGraphicsPipelineDesc>(), &desc);
	CHECK(j["rasterizerState"]["cullMode"] == "Back");   // 既定値も書く
	CHECK(j["rtvFormats"].size() == GFX_MAX_RENDER_TARGETS);
	CHECK_FALSE(j.contains("vs"));
	CHECK_FALSE(j.contains("debugName"));

	// フォームでの変更: カリングとブレンド
	j["rasterizerState"]["cullMode"] = "None";
	j["blendState"]["renderTarget"][0]["blendEnable"] = true;
	j["blendState"]["renderTarget"][0]["renderTargetWriteMask"] = Json::array({ "Red", "Alpha" });
	const auto r = studio::applyJson(j, desc);
	REQUIRE(r.ok);
	CHECK(r.errors.empty());
	CHECK(desc.rasterizerState.cullMode == GfxCullMode::None);
	CHECK(desc.blendState.renderTarget[0].blendEnable);
	CHECK(desc.blendState.renderTarget[0].renderTargetWriteMask == (GfxColorWriteMask::Red | GfxColorWriteMask::Alpha));
	CHECK(desc.vs.id == 11);
	CHECK(desc.ps.id == 12);
	CHECK(std::strcmp(desc.debugName, "Pipe") == 0);
	CHECK(desc.rtvFormats[0] == GfxFormat::B8G8R8A8_Unorm);
}

TEST_CASE("applyJson: 不正な値はエラー (フィールドのパス付き) で、記述子は変えない")
{
	GfxGraphicsPipelineDesc desc;
	const Json bad = {
		{ "rasterizerState", { { "cullMode", "Sideways" } } },
		{ "depthStencilState", { { "stencilReadMask", 999 } } },
	};
	const auto r = studio::applyJson(bad, desc);
	CHECK_FALSE(r.ok);
	REQUIRE(r.errors.size() == 2);
	// nlohmann::json のオブジェクトはキーの順に並ぶので、順番には頼らない
	const bool hasCull = r.errors[0].rfind("rasterizerState.cullMode:", 0) == 0
	                  || r.errors[1].rfind("rasterizerState.cullMode:", 0) == 0;
	CHECK(hasCull);
	CHECK(desc.rasterizerState.cullMode == GfxCullMode::Back);   // 変わっていない

	// 推奨範囲の外は警告だけで、書き込む
	const auto w = studio::applyJson(Json{ { "sampleCount", 64 } }, desc);
	CHECK(w.ok);
	CHECK(w.warnings.size() == 1);
	CHECK(desc.sampleCount == 64);
}
