// ============================================================
//  ライト (M5-5) の単体テスト: 種類の定義、値の詰め方、hlsli の生成、シーンのライトとライトリンク
//  (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/LightTypes.h"
#include "services/Scene.h"

#include <cmath>

using namespace studio;

namespace
{
const Json k_Spot = {
	{ "version", 1 }, { "label", "スポット" }, { "shape", "spot" },
	{ "params", {
		{ { "name", "color" }, { "type", "color" }, { "default", { 1, 0.5, 0.25 } } },
		{ { "name", "intensity" }, { "type", "float" }, { "default", 5 }, { "min", 0 } },
		{ { "name", "range" }, { "type", "float" }, { "default", 10 } },
		{ { "name", "cone" }, { "type", "float2" }, { "default", { 20, 30 } } },   // range の次は float4 の境目をまたぐので次の float4 へ
		{ { "name", "tint" }, { "type", "float4" } },
	} },
};
} // namespace

TEST_CASE("ライトの種類: 定義を読み、パラメーターの位置を決める")
{
	LightTypeDef t;
	LightTypeDiagnostics diag;
	REQUIRE(lightTypeFromJson("spot", k_Spot, t, diag));
	CHECK(diag.warnings.empty());
	CHECK(t.label == "スポット");
	REQUIRE(t.params.size() == 5);
	CHECK(t.params[0].offset == 0);   // color: x y z
	CHECK(t.params[1].offset == 3);   // intensity: w
	CHECK(t.params[2].offset == 4);   // range: params[1].x
	CHECK(t.params[3].offset == 5);   // cone: params[1].yz (境目をまたがない)
	CHECK(t.params[4].offset == 8);   // tint: params[2]
	CHECK(t.params[4].defaults == std::vector<float>{ 0, 0, 0, 0 });

	const Json ui = lightTypeToJson(t);
	CHECK(ui["params"][0]["type"] == "color");
	CHECK(ui["params"][1]["min"] == 0);
	CHECK(ui["params"][3]["default"] == Json({ 20, 30 }));

	// 形の違い
	LightTypeDiagnostics bad;
	CHECK_FALSE(lightTypeFromJson("1bad", { { "params", Json::array() } }, t, bad));
	CHECK_FALSE(lightTypeFromJson("x", { { "params", { { { "name", "a" }, { "type", "matrix" } } } } }, t, bad));
	CHECK_FALSE(lightTypeFromJson("x", { { "params", { { { "name", "a" } }, { { "name", "a" } } } } }, t, bad));
	Json many = { { "params", Json::array() } };
	for (int i = 0; i < 5; ++i) many["params"].push_back({ { "name", "p" + std::to_string(i) }, { "type", "float4" } });
	CHECK_FALSE(lightTypeFromJson("x", many, t, bad));   // 20 個は多すぎる
}

TEST_CASE("ライトの種類: 値を詰める (既定値、範囲、形の違い)")
{
	LightTypeDef t;
	LightTypeDiagnostics diag;
	REQUIRE(lightTypeFromJson("spot", k_Spot, t, diag));
	std::vector<std::string> warnings;
	const std::vector<float> p = packLightParams(t, { { "intensity", -3 }, { "cone", { 10, 15 } }, { "range", "far" } }, &warnings);
	REQUIRE(p.size() == k_LightParamFloats);
	CHECK(p[0] == 1.0f);     // color の既定値
	CHECK(p[1] == 0.5f);
	CHECK(p[3] == 0.0f);     // intensity は min 0 に
	CHECK(p[4] == 10.0f);    // range は形が違うので既定値
	CHECK(p[5] == 10.0f);
	CHECK(p[6] == 15.0f);
	CHECK(warnings.size() == 1);
}

TEST_CASE("ライトの種類: Lighting シェーダーが include する hlsli")
{
	LightTypeDef spot, dir;
	LightTypeDiagnostics diag;
	REQUIRE(lightTypeFromJson("spot", k_Spot, spot, diag));
	REQUIRE(lightTypeFromJson("directional", { { "params", Json::array() } }, dir, diag));
	const std::string h = generateLightsHlsli({ dir, spot });
	CHECK(h.find("#define LIGHT_TYPE_directional 0") != std::string::npos);
	CHECK(h.find("#define LIGHT_TYPE_spot 1") != std::string::npos);
	CHECK(h.find("p.cone = d.params[1].yz;") != std::string::npos);
	CHECK(h.find("p.tint = d.params[2].xyzw;") != std::string::npos);
	CHECK(h.find("#include \"../../lights/spot.light.hlsl\"") != std::string::npos);
	CHECK(h.find("case LIGHT_TYPE_spot: return Light_spot(s, b, unpackLight_spot(d));") != std::string::npos);
	CHECK(h.find("float _unused;") != std::string::npos);   // パラメーターのない種類
	CHECK(h.find("cbuffer Lights : register(b2)") != std::string::npos);
}

TEST_CASE("シーン: ライトの物体の読み書き")
{
	const Json j = { { "version", 1 },
		{ "objects", {
			{ { "id", "hero" }, { "name", "Hero" } },
			{ { "id", "rim" }, { "name", "Rim" }, { "model", "" },
			  { "light", { { "type", "spot" }, { "params", { { "intensity", 3 } } }, { "links", { "hero" } } } } },
		} } };
	SceneDesc s;
	SceneDiagnostics diag;
	REQUIRE(sceneFromJson(j, s, diag));
	CHECK(diag.warnings.empty());   // ライトの物体は model が空でも警告しない
	REQUIRE(s.objects[1].light);
	CHECK(s.objects[1].light->type == "spot");
	CHECK(s.objects[1].light->links == std::vector<std::string>{ "hero" });
	const Json out = sceneToJson(s);
	CHECK(out["objects"][1]["light"]["params"]["intensity"] == 3);
	CHECK_FALSE(out["objects"][0].contains("light"));

	SceneDiagnostics bad;
	CHECK_FALSE(sceneFromJson({ { "objects", { { { "id", "a" }, { "light", { { "type", 3 } } } } } } }, s, bad));
	CHECK_FALSE(sceneFromJson({ { "objects", { { { "id", "a" }, { "light", { { "links", "hero" } } } } } } }, s, bad));

	// 既定のシーンには、今までと同じ向きの平行光源がある
	const SceneDesc def = defaultScene();
	const SceneObject& sun = def.objects.back();
	REQUIRE(sun.light);
	CHECK(sun.light->type == "directional");
	const auto world = worldMatrices(def);
	const auto d = lightDirection(world.back());
	CHECK(d[0] == doctest::Approx(-0.45f / 1.13688f).epsilon(0.01));
	CHECK(d[1] == doctest::Approx(-1.0f / 1.13688f).epsilon(0.01));
	CHECK(d[2] == doctest::Approx(-0.30f / 1.13688f).epsilon(0.01));
}

TEST_CASE("シーン: ライトリンクのマスク")
{
	SceneDesc s;
	auto obj = [](const char* id, const char* parent = "") { SceneObject o; o.id = id; o.parent = parent; return o; };
	auto light = [](const char* id, std::vector<std::string> links) {
		SceneObject o;
		o.id    = id;
		o.model = "";
		o.light = SceneLight{ "point", Json::object(), std::move(links) };
		return o;
	};
	s.objects = { obj("hero"), obj("hair", "hero"), obj("bg"), light("sun", {}), light("rim", { "hero" }), light("bgLight", { "bg", "missing" }) };
	const LightLinks l = computeLightLinks(s);
	CHECK(l.lightMasks[3] == k_LightMaskAll);   // links なし: すべて
	CHECK(l.lightMasks[4] == 1u);
	CHECK(l.lightMasks[5] == 2u);
	CHECK(l.objectMasks[0] == 1u);   // hero
	CHECK(l.objectMasks[1] == 1u);   // hair (hero の子)
	CHECK(l.objectMasks[2] == 2u);   // bg
	CHECK(l.objectMasks[3] == 0u);   // ライト自身
	REQUIRE(l.warnings.size() == 1); // missing
	CHECK(l.warnings[0].find("missing") != std::string::npos);

	// 33 個目のライトはすべてを照らす (警告)
	SceneDesc many;
	many.objects.push_back(obj("hero"));
	for (int i = 0; i < 33; ++i)
		many.objects.push_back(light(("l" + std::to_string(i)).c_str(), { "hero" }));
	const LightLinks m = computeLightLinks(many);
	CHECK(m.objectMasks[0] == 0xFFFFFFFFu);
	CHECK(m.lightMasks[33] == k_LightMaskAll);
	CHECK(m.warnings.size() == 1);
}
