// ============================================================
//  描画ノードのリソースの値 (詰め方、取り込んだ値、決まる順、シーンの読み書き) の単体テスト (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/NodeParams.h"
#include "services/Scene.h"

#include <cstring>

using namespace studio;

namespace
{
float f32(const std::vector<uint8_t>& b, size_t off)
{
	float v = 0;
	std::memcpy(&v, b.data() + off, 4);
	return v;
}

// cbuffer Params { float4 baseColor; float roughness; int mode; float2 tiling; }
CBufferLayout params()
{
	CBufferLayout l;
	l.name = "Params";
	l.reg  = 2;
	l.size = 48;
	l.vars = { { "baseColor", 0, 16, CBufferVar::Kind::Float, 1, 4, 0 },
	           { "roughness", 16, 4, CBufferVar::Kind::Float, 1, 1, 0 },
	           { "mode", 20, 4, CBufferVar::Kind::Int, 1, 1, 0 },
	           { "tiling", 24, 8, CBufferVar::Kind::Float, 1, 2, 0 } };
	return l;
}
} // namespace

TEST_CASE("描画ノードのリソース: cbuffer に詰める (書いていない変数は 0、ほかの変数とテクスチャは無視)")
{
	std::vector<std::string> warnings;
	const auto b = packCBuffer(params(), { { "baseColor", { 1, 0.5, 0.25, 1 } }, { "mode", 3 }, { "tiling", { 2, 4 } },
	                                       { "baseColorMap", "asset:0123456789abcdef0123456789abcdef" }, { "other", 1 } }, &warnings);
	REQUIRE(b.size() == 48);
	CHECK(f32(b, 0) == 1.0f);
	CHECK(f32(b, 4) == 0.5f);
	CHECK(f32(b, 16) == 0.0f);   // roughness は書いていない
	// 書いていない色の変数は 1
	const auto d = packCBuffer(params(), Json::object());
	CHECK(f32(d, 0) == 1.0f);
	CHECK(f32(d, 12) == 1.0f);
	CHECK(f32(d, 16) == 0.0f);
	int32_t mode = 0;
	std::memcpy(&mode, b.data() + 20, 4);
	CHECK(mode == 3);
	CHECK(f32(b, 28) == 4.0f);
	CHECK(warnings.empty());
	// 足りない値は警告、長い値 (float4 → float2) は先頭を使う
	warnings.clear();
	const auto c = packCBuffer(params(), { { "baseColor", { 1, 2 } }, { "tiling", { 7, 8, 9, 10 } } }, &warnings);
	CHECK(f32(c, 24) == 7.0f);
	CHECK(warnings.size() == 1);
}

TEST_CASE("描画ノードのリソース: 取り込んだ値 → 物体 → スロットの順に決まる")
{
	ImportedSlot s;
	s.baseColor        = { 0.8f, 0.4f, 0.2f, 1.0f };
	s.roughness        = 0.3f;
	s.baseColorTexture = "0123456789abcdef0123456789abcdef";
	const Json im = importedValues(s);
	CHECK(im["baseColor"] == Json({ 0.8, 0.4, 0.2, 1.0 }));
	CHECK(im["albedo"] == im["baseColor"]);
	CHECK(im["baseColorMap"] == "asset:0123456789abcdef0123456789abcdef");
	CHECK(im["mainTex"] == im["baseColorMap"]);
	CHECK_FALSE(im.contains("normalMap"));   // 取り込まれていないテクスチャは書かない (既定のテクスチャ)

	const Json obj  = { { "roughness", 0.9 }, { "baseColorMap", nullptr } };   // null = 取り込んだ値も使わない
	const Json slot = { { "roughness", 0.1 } };
	const Json v = mergeNodeParams(im, obj, slot);
	CHECK(v["roughness"] == 0.1);
	CHECK_FALSE(v.contains("baseColorMap"));
	CHECK(v["baseColor"] == im["baseColor"]);
	CHECK(mergeNodeParams(nullptr, nullptr, nullptr) == Json::object());
}

TEST_CASE("描画ノードのリソース: テクスチャの名前の決まり")
{
	CHECK(isColorTextureName("baseColorMap"));
	CHECK(isColorTextureName("AlbedoTex"));
	CHECK(isColorTextureName("mainTex"));
	CHECK_FALSE(isColorTextureName("normalMap"));
	CHECK(defaultTextureFor("normalMap") == DefaultTexture::FlatNormal);
	CHECK(defaultTextureFor("roughnessMap") == DefaultTexture::White);
	CHECK(textureAssetId("asset:0123456789abcdef0123456789abcdef") == "0123456789abcdef0123456789abcdef");
	CHECK(textureAssetId("asset:XYZ").empty());
	CHECK(textureAssetId("materials/a.png").empty());
}

TEST_CASE("描画ノードのリソース: シーンの nodeParams / slotNodeParams を読み書きする")
{
	const Json j = { { "version", 1 }, { "objects", Json::array({ {
		{ "id", "a" }, { "name", "A" }, { "model", "builtin:cube" },
		{ "nodeParams", { { "Draw", { { "baseColor", { 1, 0, 0, 1 } }, { "baseColorMap", "asset:0123456789abcdef0123456789abcdef" } } } } },
		{ "slotNodeParams", { { "Default", { { "Draw", { { "roughness", 0.2 } } } } } } },
	} }) } };
	SceneDesc s;
	SceneDiagnostics diag;
	REQUIRE(sceneFromJson(j, s, diag));
	CHECK(diag.warnings.empty());
	const SceneObject& o = s.objects[0];
	CHECK(objectNodeParams(o, "Draw")["baseColor"] == Json({ 1, 0, 0, 1 }));
	CHECK(slotNodeParams(o, "Default", "Draw")["roughness"] == 0.2);
	CHECK(objectNodeParams(o, "Other").is_null());
	CHECK(slotNodeParams(o, "Body", "Draw").is_null());
	const Json back = sceneToJson(s);
	CHECK(back["objects"][0]["nodeParams"] == j["objects"][0]["nodeParams"]);
	CHECK(back["objects"][0]["slotNodeParams"] == j["objects"][0]["slotNodeParams"]);

	// 形が違えばエラー
	Json bad = j;
	bad["objects"][0]["nodeParams"]["Draw"]["x"] = Json::object();
	SceneDesc s2;
	SceneDiagnostics d2;
	CHECK_FALSE(sceneFromJson(bad, s2, d2));
}
