// ============================================================
//  アセットのライブラリ (ProjectAssets、M5-3) の単体テスト (GPU / webview 不要)
//  取り込むファイル (OBJ + .mtl + テクスチャ) はテストの中で書く。
// ============================================================
#include <doctest/doctest.h>
#include "services/ProjectAssets.h"
#include "services/Scene.h"

#include <algorithm>
#include <fstream>

using namespace studio;
namespace fs = std::filesystem;

namespace
{
fs::path freshDir(const char* name)
{
	const fs::path p = fs::temp_directory_path() / name;
	std::error_code ec;
	fs::remove_all(p, ec);
	fs::create_directories(p);
	return p;
}

void writeText(const fs::path& p, const std::string& text)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary).write(text.data(), static_cast<std::streamsize>(text.size()));
}

// 2 × 2 の非圧縮 TGA
std::string tga()
{
	std::string t(18, '\0');
	t[2] = 2; t[12] = 2; t[14] = 2; t[16] = 32; t[17] = 0x28;
	t.append(16, '\x7f');
	return t;
}

bool contains(const std::vector<std::string>& v, const std::string& s)
{
	return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

TEST_CASE("ライブラリ: OBJ をコピーで取り込み、.mtl とテクスチャも一緒にコピーする")
{
	const fs::path src = freshDir("studio_lib_src"), project = freshDir("studio_lib_project");
	writeText(src / "box.obj", "mtllib box.mtl\nv 0 0 0\nv 2 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl Red\nf 1/1 2/2 3/3\n");
	writeText(src / "box.mtl", "newmtl Red\nKd 1 0 0\nmap_Kd tex/red.tga\n");
	writeText(src / "tex" / "red.tga", tga());

	ProjectAssets lib;
	sunset::asset::Diagnostics diag;
	lib.open(project, diag);
	REQUIRE(lib.isOpen());
	CHECK(fs::is_directory(project / "assets"));

	ProjectAssets::ImportResult r;
	std::string error;
	REQUIRE_MESSAGE(lib.importCopy(src / "box.obj", r, error), error);
	CHECK(r.path == "box/box.obj");
	CHECK(contains(r.copied, "box/box.mtl"));
	CHECK(contains(r.copied, "box/tex/red.tga"));
	CHECK(fs::exists(project / "assets" / "box" / "box.obj.meta"));
	CHECK(fs::exists(project / "assets" / "box" / "tex" / "red.tga.meta"));   // テクスチャもアセットになる
	REQUIRE(r.id.size() == 32);
	CHECK(assetIdOfModel("asset:" + r.id) == r.id);

	// 一覧
	const nlohmann::json list = lib.listJson();
	REQUIRE(list.size() == 2);
	CHECK(list[0]["path"] == "box/box.obj");
	CHECK(list[0]["importer"] == "model");
	CHECK(list[0]["cached"] == false);
	CHECK(list[1]["importer"] == "texture");

	// 同じものをもう一度: 名前を変えて別のフォルダに
	ProjectAssets::ImportResult r2;
	REQUIRE(lib.importCopy(src / "box.obj", r2, error));
	CHECK(r2.path == "box-2/box.obj");
	CHECK(r2.id != r.id);

	// モデルを読む (キャッシュに入れる)
	const sunset::asset::AssetRecord* rec = lib.find(r.id);
	REQUIRE(rec);
	const auto model = lib.loadModel(*rec, lib.cacheDir(), diag);
	const std::string firstError = diag.errors.empty() ? std::string() : diag.errors[0];
	REQUIRE_MESSAGE(model, firstError);
	REQUIRE(model->slots.size() == 1);
	CHECK(model->slots[0].baseColor[0] == doctest::Approx(1.0f));
	CHECK(model->boundsMax[0] == doctest::Approx(2.0f));
	CHECK(fs::exists(project / "Cache" / "assets" / (r.id + ".bin")));
	for (const auto& a : lib.listJson())   // パスの順 ("box-2/" が "box/" より前)
		CHECK(a["cached"] == (a["id"] == r.id));

	// テクスチャはモデルとして読めない
	const sunset::asset::AssetRecord* texRec = lib.database().findByPath("box/tex/red.tga");
	REQUIRE(texRec);
	sunset::asset::Diagnostics texDiag;
	CHECK_FALSE(lib.loadModel(*texRec, lib.cacheDir(), texDiag));
	CHECK_FALSE(texDiag.ok());

	// 閉じて開き直しても同じ ID (.meta から)
	lib.close();
	CHECK_FALSE(lib.isOpen());
	lib.open(project, diag);
	REQUIRE(lib.find(r.id));
	CHECK(lib.find(r.id)->path == "box/box.obj");

	std::error_code ec;
	fs::remove_all(src, ec);
	fs::remove_all(project, ec);
}

TEST_CASE("ライブラリ: 外部のファイルを参照する (コピーしない)")
{
	const fs::path src = freshDir("studio_lib_ext_src"), project = freshDir("studio_lib_ext_project");
	writeText(src / "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
	ProjectAssets lib;
	sunset::asset::Diagnostics diag;
	lib.open(project, diag);
	ProjectAssets::ImportResult r;
	std::string error;
	REQUIRE_MESSAGE(lib.importLink(src / "tri.obj", r, error), error);
	CHECK(r.path == "tri.obj");
	CHECK_FALSE(fs::exists(project / "assets" / "tri.obj"));
	CHECK(fs::exists(project / "assets" / "tri.obj.meta"));
	const nlohmann::json list = lib.listJson();
	REQUIRE(list.size() == 1);
	CHECK(list[0]["external"] == true);
	CHECK(list[0]["missing"] == false);

	// 取り込めない種類
	writeText(src / "notes.txt", "x");
	CHECK_FALSE(lib.importLink(src / "notes.txt", r, error));
	CHECK_FALSE(lib.importCopy(src / "notes.txt", r, error));

	// 元のファイルがなくなると missing
	fs::remove(src / "tri.obj");
	lib.rescan(diag);
	CHECK(lib.listJson()[0]["missing"] == true);

	std::error_code ec;
	fs::remove_all(src, ec);
	fs::remove_all(project, ec);
}

TEST_CASE("ライブラリ: シーンは asset:<ID> のモデルを受け付ける")
{
	CHECK(assetIdOfModel("asset:0123456789abcdef0123456789abcdef") == "0123456789abcdef0123456789abcdef");
	CHECK_FALSE(assetIdOfModel("asset:0123"));
	CHECK_FALSE(assetIdOfModel("asset:0123456789ABCDEF0123456789abcdef"));   // 小文字だけ
	CHECK_FALSE(assetIdOfModel("builtin:cube"));

	const nlohmann::json j = { { "version", 1 },
		{ "objects", { { { "id", "m" }, { "name", "M" }, { "model", "asset:0123456789abcdef0123456789abcdef" } },
		               { { "id", "x" }, { "name", "X" }, { "model", "hero.fbx" } } } } };
	SceneDesc scene;
	SceneDiagnostics diag;
	REQUIRE(sceneFromJson(j, scene, diag));
	REQUIRE(diag.warnings.size() == 1);   // 知らない形は x だけ
	CHECK(diag.warnings[0].rfind("objects[1].model", 0) == 0);
}
