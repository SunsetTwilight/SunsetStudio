// ============================================================
//  シーンのファイル (読み書き、診断、行列、選択) の単体テスト (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/Scene.h"

#include <cmath>

using namespace studio;

namespace
{
bool hasPrefix(const std::vector<std::string>& v, const char* prefix)
{
	for (const auto& s : v)
		if (s.rfind(prefix, 0) == 0)
			return true;
	return false;
}

// 列優先の行列で点を変換する
std::array<float, 3> apply(const Matrix& m, std::array<float, 3> p)
{
	return { m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
	         m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] };
}

SceneObject obj(const char* id, std::array<float, 3> pos, const char* parent = "")
{
	SceneObject o;
	o.id       = id;
	o.name     = id;
	o.position = pos;
	o.parent   = parent;
	return o;
}
} // namespace

TEST_CASE("シーン: JSON の読み書きと診断")
{
	SceneDesc s = defaultScene();
	CHECK(s.objects.size() == 50);   // 立方体 49 個 + 平行光源
	CHECK(s.objects[0].id == "center");
	s.editor = { { "camera", 1 } };
	SceneDesc back;
	SceneDiagnostics diag;
	REQUIRE(sceneFromJson(sceneToJson(s), back, diag));
	CHECK(diag.warnings.empty());
	CHECK(back.objects.size() == 50);
	CHECK(back.objects[0].scale == std::array<float, 3>{ 3, 3, 3 });
	CHECK(back.editor["camera"] == 1);
	CHECK(sceneToJson(back)["objects"][0]["parent"].is_null());

	// 重複した id、ない親、形の違い、知らないキー
	const Json bad = { { "objects", Json::array({
		{ { "id", "a" }, { "position", { 1, 2 } } },
		{ { "id", "a" }, { "parent", "missing" }, { "future", 1 } },
		{ { "id", "b c" } },
	}) } };
	SceneDiagnostics d2;
	CHECK_FALSE(sceneFromJson(bad, back, d2));
	CHECK(hasPrefix(d2.errors, "objects[0].position"));
	CHECK(hasPrefix(d2.errors, "objects[1].id"));
	CHECK(hasPrefix(d2.errors, "objects[1].parent"));
	CHECK(hasPrefix(d2.errors, "objects[2].id"));
	CHECK(hasPrefix(d2.warnings, "objects[1].future"));

	// 親子の循環
	const Json cycle = { { "objects", Json::array({ { { "id", "a" }, { "parent", "b" } }, { { "id", "b" }, { "parent", "a" } } }) } };
	SceneDiagnostics d3;
	CHECK_FALSE(sceneFromJson(cycle, back, d3));
	CHECK(hasPrefix(d3.errors, "objects[0].parent"));

	// 知らないモデルは警告
	SceneDiagnostics d4;
	REQUIRE(sceneFromJson({ { "objects", Json::array({ { { "id", "m" }, { "model", "assets/x.fbx" } } }) } }, back, d4));
	CHECK(hasPrefix(d4.warnings, "objects[0].model"));
}

TEST_CASE("シーン: ローカル / ワールドの行列と、見えるか")
{
	SceneDesc s;
	SceneObject parent = obj("p", { 10, 0, 0 });
	parent.rotation = { 0, 90, 0 };   // Y で 90 度: +X → -Z
	parent.scale    = { 2, 2, 2 };
	s.objects.push_back(parent);
	s.objects.push_back(obj("c", { 1, 0, 0 }, "p"));
	const auto w = worldMatrices(s);
	const auto p = apply(w[1], { 0, 0, 0 });   // 子の原点 = 親 (10,0,0) + 2 × (+X を Y で 90 度 = -Z)
	CHECK(p[0] == doctest::Approx(10.0f));
	CHECK(p[1] == doctest::Approx(0.0f));
	CHECK(p[2] == doctest::Approx(-2.0f));

	// X → Y → Z の順: X 90 のあと Z 90。+Y は X 90 で +Z、Z 90 では +Z のまま
	SceneObject r = obj("r", { 0, 0, 0 });
	r.rotation = { 90, 0, 90 };
	const auto q = apply(localMatrix(r), { 0, 1, 0 });
	CHECK(q[0] == doctest::Approx(0.0f).epsilon(1e-5));
	CHECK(q[1] == doctest::Approx(0.0f).epsilon(1e-5));
	CHECK(q[2] == doctest::Approx(1.0f));

	s.objects[0].visible = false;
	const auto v = effectiveVisibility(s);
	CHECK_FALSE(v[0]);
	CHECK_FALSE(v[1]);   // 親が見えなければ子も
}

TEST_CASE("シーン: クリックの選択 (光線と箱)")
{
	SceneDesc s = defaultScene();
	s.objects.pop_back();   // 平行光源 (y = 8 の上にある) は外す。ライトの選択の箱はビューポートが決める
	const auto w = worldMatrices(s);
	// 真上から中央へ: 中央の大きな立方体
	CHECK(pickObject(s, w, { 0, 20, 0 }, { 0, -1, 0 }) == 0);
	// 横から: 手前の立方体が先に当たる
	const int i = pickObject(s, w, { -30, 0.2f, 0 }, { 1, 0, 0 });
	REQUIRE(i >= 0);
	CHECK(s.objects[static_cast<size_t>(i)].id == "cube_0_3");   // x = -3 の列
	// 何もないところ
	CHECK(pickObject(s, w, { 0, 20, 0 }, { 0, 1, 0 }) == -1);
	// 見えない物は選ばない
	s.objects[0].visible = false;
	CHECK(pickObject(s, w, { 0, 20, 0 }, { 0, -1, 0 }) == -1);

	// 回した箱: Y で 45 度回した 1 x 1 x 1 の箱の角 (0.6, 0, 0) は当たる (対角の半分 ≈ 0.707)
	SceneDesc one;
	SceneObject b = obj("b", { 0, 0, 0 });
	b.rotation = { 0, 45, 0 };
	one.objects.push_back(b);
	const auto w1 = worldMatrices(one);
	CHECK(pickObject(one, w1, { 0.6f, 5, 0 }, { 0, -1, 0 }) == 0);
	CHECK(pickObject(one, w1, { 0.75f, 5, 0 }, { 0, -1, 0 }) == -1);

	CHECK(uniqueObjectId(s, "cube") == "cube-1");
}
