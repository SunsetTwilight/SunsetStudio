// ============================================================
//  マテリアル (M5-4) の単体テスト: ファイルの読み書きと診断、定数バッファへの詰め方、テクスチャの名前の決まり、
//  シーンのスロットへの割り当て (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/Material.h"
#include "services/Scene.h"

#include <cstring>

using namespace studio;

namespace
{
float f32(const std::vector<uint8_t>& b, uint32_t off)
{
	float v = 0;
	std::memcpy(&v, b.data() + off, 4);
	return v;
}
uint32_t u32(const std::vector<uint8_t>& b, uint32_t off)
{
	uint32_t v = 0;
	std::memcpy(&v, b.data() + off, 4);
	return v;
}
const char* k_Id = "0123456789abcdef0123456789abcdef";
} // namespace

TEST_CASE("マテリアル: 読み書きと診断")
{
	const Json j = {
		{ "version", 1 },
		{ "passes", { { "Opaque", "pipelines/materials/standard.gbuffer.json" } } },
		{ "params", { { "baseColor", { 1, 0.5, 0.25, 1 } }, { "roughness", 0.4 } } },
		{ "textures", { { "baseColorMap", std::string("asset:") + k_Id }, { "normalMap", nullptr } } },
	};
	MaterialDesc m;
	MaterialDiagnostics diag;
	REQUIRE(materialFromJson(j, m, diag));
	CHECK(diag.warnings.empty());
	CHECK(m.passes.at("Opaque") == "pipelines/materials/standard.gbuffer.json");
	CHECK(m.textures.size() == 1);   // null は割り当てなし
	CHECK(m.params["roughness"] == 0.4);

	MaterialDesc back;
	MaterialDiagnostics d2;
	REQUIRE(materialFromJson(materialToJson(m), back, d2));
	CHECK(materialToJson(back) == materialToJson(m));

	// 形の違い
	MaterialDiagnostics bad;
	CHECK_FALSE(materialFromJson({ { "passes", { { "Opaque", 3 } } } }, m, bad));
	CHECK_FALSE(materialFromJson({ { "textures", { { "baseColorMap", "hero.png" } } } }, m, bad));
	CHECK_FALSE(materialFromJson({ { "version", 99 } }, m, bad));
	MaterialDiagnostics warn;
	CHECK(materialFromJson({ { "passes", Json::object() }, { "shader", "x" } }, m, warn));
	CHECK(warn.warnings.size() == 2);   // 知らないキー、パスがない
}

TEST_CASE("マテリアル: パラメーターを cbuffer の配置に詰める")
{
	// cbuffer Material { float4 baseColor; float metallic; float roughness; uint flags; float3 tint[2]; }
	CBufferLayout layout;
	layout.name = "Material";
	layout.size = 64;
	layout.vars = {
		{ "baseColor", 0, 16, CBufferVar::Kind::Float, 1, 4, 0 },
		{ "metallic", 16, 4, CBufferVar::Kind::Float, 1, 1, 0 },
		{ "roughness", 20, 4, CBufferVar::Kind::Float, 1, 1, 0 },
		{ "flags", 24, 4, CBufferVar::Kind::UInt, 1, 1, 0 },
		{ "tint", 32, 28, CBufferVar::Kind::Float, 1, 3, 2 },
	};
	const Json params = {
		{ "baseColor", { 1, 0.5, 0.25, 1 } }, { "roughness", 0.75 }, { "flags", true },
		{ "tint", { 1, 2, 3, 4, 5, 6 } }, { "unknown", 1 }, { "metallic", "x" },
	};
	std::vector<std::string> warnings;
	const std::vector<uint8_t> b = packMaterialParams(layout, params, &warnings);
	REQUIRE(b.size() == 64);
	CHECK(f32(b, 0) == 1.0f);
	CHECK(f32(b, 4) == 0.5f);
	CHECK(f32(b, 12) == 1.0f);
	CHECK(f32(b, 16) == 0.0f);    // 文字列は書かない (警告)
	CHECK(f32(b, 20) == 0.75f);
	CHECK(u32(b, 24) == 1u);      // 真偽値 → 1
	CHECK(f32(b, 32) == 1.0f);    // 配列の要素は 16 バイトごと
	CHECK(f32(b, 40) == 3.0f);
	CHECK(f32(b, 48) == 4.0f);
	CHECK(f32(b, 56) == 6.0f);
	CHECK(warnings.size() == 2);  // unknown と metallic

	// 値の数が違えば、書けるところまで書いて警告
	warnings.clear();
	const std::vector<uint8_t> b2 = packMaterialParams(layout, { { "baseColor", { 0.5, 0.5 } } }, &warnings);
	CHECK(f32(b2, 4) == 0.5f);
	CHECK(f32(b2, 8) == 0.0f);
	CHECK(warnings.size() == 1);
}

TEST_CASE("マテリアル: テクスチャの名前の決まり")
{
	CHECK(isColorTextureName("baseColorMap"));
	CHECK(isColorTextureName("AlbedoTex"));
	CHECK(isColorTextureName("emissiveMap"));
	CHECK_FALSE(isColorTextureName("normalMap"));
	CHECK_FALSE(isColorTextureName("metallicRoughnessMap"));
	CHECK(defaultTextureFor("normalMap") == DefaultTexture::FlatNormal);
	CHECK(defaultTextureFor("baseColorMap") == DefaultTexture::White);
	CHECK(textureAssetId(std::string("asset:") + k_Id) == k_Id);
	CHECK(textureAssetId("hero.png").empty());
}

TEST_CASE("シーン: スロットへのマテリアルの割り当て")
{
	const Json j = { { "version", 1 },
		{ "objects", { { { "id", "a" }, { "name", "A" }, { "layer", "Opaque" },
		                 { "materials", { { "Body", "materials/skin.material.json" }, { "Hair", nullptr } } } } } } };
	SceneDesc s;
	SceneDiagnostics diag;
	REQUIRE(sceneFromJson(j, s, diag));
	CHECK(diag.warnings.empty());   // 古い layer は黙って読み飛ばす
	REQUIRE(s.objects.size() == 1);
	CHECK(s.objects[0].materials.size() == 1);
	CHECK(s.objects[0].materials.at("Body") == "materials/skin.material.json");
	const Json out = sceneToJson(s);
	CHECK(out["objects"][0]["materials"]["Body"] == "materials/skin.material.json");
	CHECK_FALSE(out["objects"][0].contains("layer"));

	// 割り当てがなければ書かない
	s.objects[0].materials.clear();
	CHECK_FALSE(sceneToJson(s)["objects"][0].contains("materials"));

	SceneDiagnostics bad;
	CHECK_FALSE(sceneFromJson({ { "objects", { { { "id", "a" }, { "materials", { { "Body", 3 } } } } } } }, s, bad));
	CHECK_FALSE(sceneFromJson({ { "objects", { { { "id", "a" }, { "materials", "x" } } } } }, s, bad));
}
