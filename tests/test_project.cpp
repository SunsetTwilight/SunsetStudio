// ============================================================
//  プロジェクトファイル (.sunsetproj) と最近のプロジェクトの単体テスト (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/Project.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

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

void writeText(const fs::path& p, const std::string& text)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

bool hasPrefix(const std::vector<std::string>& v, const char* prefix)
{
	for (const auto& s : v)
		if (s.rfind(prefix, 0) == 0)
			return true;
	return false;
}
} // namespace

TEST_CASE("プロジェクト: JSON の読み書きと診断")
{
	ProjectDesc d;
	d.name   = "MyGame";
	d.engine = "0.3";
	ProjectDesc back;
	ProjectDiagnostics diag;
	REQUIRE(projectFromJson(projectToJson(d), back, diag));
	CHECK(back.name == "MyGame");
	CHECK(back.engine == "0.3");
	CHECK(back.version == 2);
	CHECK(back.assets == ".");   // version 2 はプロジェクトのフォルダが起点
	CHECK_FALSE(back.legacyLayout());
	CHECK_FALSE(projectToJson(d).contains("assets"));
	CHECK(back.viewportGraph == "graphs/viewport.json");
	CHECK(diag.warnings.empty());

	// version 1 (M4a): assets があればそれ、なければ Assets
	ProjectDiagnostics d1;
	REQUIRE(projectFromJson({ { "version", 1 }, { "name", "Old" }, { "assets", "Assets" } }, back, d1));
	CHECK(back.legacyLayout());
	CHECK(back.assets == "Assets");
	CHECK(projectToJson(back)["assets"] == "Assets");
	REQUIRE(projectFromJson({ { "name", "NoVersion" } }, back, d1));
	CHECK(back.version == 1);
	CHECK(back.assets == "Assets");
	// version 2 の assets は無視して警告
	ProjectDiagnostics d1w;
	REQUIRE(projectFromJson({ { "version", 2 }, { "assets", "Elsewhere" } }, back, d1w));
	CHECK(back.assets == ".");
	CHECK(hasPrefix(d1w.warnings, "assets"));

	// 型の違いはエラー、知らないキーは警告
	ProjectDiagnostics d2;
	const Json bad = { { "version", "one" }, { "assets", 3 }, { "future", true }, { "viewport", { { "graph", "" } } } };
	CHECK_FALSE(projectFromJson(bad, back, d2));
	CHECK(hasPrefix(d2.errors, "version"));
	CHECK(hasPrefix(d2.errors, "assets"));
	CHECK(hasPrefix(d2.errors, "viewport.graph"));
	CHECK(hasPrefix(d2.warnings, "future"));

	ProjectDiagnostics d3;
	CHECK_FALSE(projectFromJson({ { "assets", "C:/abs" } }, back, d3));
	CHECK(hasPrefix(d3.errors, "assets"));
}

TEST_CASE("プロジェクト: テンプレートから作って開く")
{
	const fs::path root = freshDir("studio_tests_project");
	const fs::path tmpl = root / "template";
	writeText(tmpl / "graphs" / "viewport.json", "{}");
	writeText(tmpl / "shaders" / "a.hlsl", "// a");

	Project p;
	std::string error;
	REQUIRE_MESSAGE(createProject(root / "projects", "MyGame", tmpl, "0.3", p, error), error);
	CHECK(p.file == (root / "projects" / "MyGame" / "MyGame.sunsetproj").lexically_normal());
	CHECK(p.assetRoot() == p.dir());   // version 2: 直下に並べる
	CHECK(fs::exists(p.dir() / "graphs" / "viewport.json"));
	CHECK(fs::exists(p.dir() / "shaders" / "a.hlsl"));
	for (const char* d : { "scenes", "materials", "lights", "pipelines", "assets" })
		CHECK_MESSAGE(fs::is_directory(p.dir() / d), d);
	CHECK_FALSE(fs::exists(p.dir() / "Assets" / "graphs"));
	CHECK(fs::is_directory(p.savedDir()));
	{
		std::ifstream f(p.dir() / ".gitignore");
		std::string ignore((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		CHECK(ignore.find("Saved/") != std::string::npos);
		CHECK(ignore.find("Cache/") != std::string::npos);
	}

	// 開く
	Project opened;
	ProjectDiagnostics diag;
	REQUIRE(loadProject(p.file, opened, diag, "0.3"));
	CHECK(opened.desc.name == "MyGame");
	CHECK(opened.assetRoot() == p.assetRoot());
	CHECK(diag.warnings.empty());

	// 新しいエンジンで作られたプロジェクトは警告
	ProjectDiagnostics newer;
	REQUIRE(loadProject(p.file, opened, newer, "0.2"));
	CHECK(hasPrefix(newer.warnings, "engine"));

	// 同じ場所にもう一度は作れない、名前の検査
	Project again;
	CHECK_FALSE(createProject(root / "projects", "MyGame", tmpl, "0.3", again, error));
	CHECK_FALSE(createProject(root / "projects", "a/b", tmpl, "0.3", again, error));
	CHECK_FALSE(isValidProjectName(""));
	CHECK_FALSE(isValidProjectName("CON"));
	CHECK_FALSE(isValidProjectName("x."));
	CHECK(isValidProjectName("My Game 2"));

	// version 1 でアセットのフォルダがない / 壊れた JSON
	writeText(p.file, R"({ "version": 1, "name": "MyGame", "assets": "Missing" })");
	ProjectDiagnostics noAssets;
	CHECK_FALSE(loadProject(p.file, opened, noAssets));
	CHECK(hasPrefix(noAssets.errors, "assets"));
	writeText(p.file, "{ broken");
	ProjectDiagnostics broken;
	CHECK_FALSE(loadProject(p.file, opened, broken));
	CHECK(hasPrefix(broken.errors, "JSON"));

	fs::remove_all(root);
}

TEST_CASE("プロジェクト: 最近のプロジェクトの一覧")
{
	const fs::path root = freshDir("studio_tests_recent");
	const fs::path file = root / "recent.json";

	auto make = [&](const char* name) {
		Project p;
		p.file      = root / name / (std::string(name) + ".sunsetproj");
		p.desc.name = name;
		writeText(p.file, "{}");
		return p;
	};
	const Project a = make("A"), b = make("B"), c = make("C");

	RecentProjects recent(file, 2);
	recent.add(a);
	recent.add(b);
	recent.add(a);   // 先頭に移る
	REQUIRE(recent.entries().size() == 2);
	CHECK(recent.entries()[0].name == "A");
	CHECK(recent.entries()[1].name == "B");
	recent.add(c);   // 最大 2 件
	CHECK(recent.entries().size() == 2);
	CHECK(recent.entries()[0].name == "C");
	REQUIRE(recent.save());

	fs::remove(c.file);
	RecentProjects loaded(file, 2);
	loaded.load();
	REQUIRE(loaded.entries().size() == 2);
	CHECK_FALSE(loaded.entries()[0].exists);   // C は消えた
	CHECK(loaded.entries()[1].exists);
	loaded.remove(c.file);
	CHECK(loaded.entries().size() == 1);

	// 壊れたファイルは空から始める
	writeText(file, "not json");
	RecentProjects broken(file);
	broken.load();
	CHECK(broken.entries().empty());

	fs::remove_all(root);
}

namespace
{
// M4a (version 1) の形のプロジェクトを作る
fs::path makeLegacyProject(const fs::path& dir)
{
	writeText(dir / "Old.sunsetproj", R"({ "version": 1, "name": "Old", "engine": "0.4", "assets": "Assets", "viewport": { "graph": "graphs/viewport.json" } })");
	writeText(dir / "Assets" / "graphs" / "viewport.json", "{}");
	writeText(dir / "Assets" / "shaders" / "a.hlsl", "// a");
	writeText(dir / "Saved" / "Layout.json", "{}");
	writeText(dir / ".gitignore", "Saved/\n");
	return dir / "Old.sunsetproj";
}
} // namespace

TEST_CASE("プロジェクト: version 1 (Assets/) を開いて、新しい構成に移す")
{
	const fs::path root = freshDir("studio_tests_project_migrate");
	const fs::path file = makeLegacyProject(root / "Old");

	Project p;
	ProjectDiagnostics diag;
	REQUIRE(loadProject(file, p, diag));
	CHECK(p.desc.legacyLayout());
	CHECK(p.assetRoot() == (root / "Old" / "Assets").lexically_normal());

	std::vector<std::string> moved;
	std::string error;
	REQUIRE_MESSAGE(migrateProjectLayout(p, moved, error), error);
	std::sort(moved.begin(), moved.end());
	CHECK(moved == std::vector<std::string>{ "graphs", "shaders" });
	CHECK_FALSE(p.desc.legacyLayout());
	CHECK(p.assetRoot() == p.dir());
	CHECK(fs::exists(p.dir() / "graphs" / "viewport.json"));
	CHECK(fs::exists(p.dir() / "shaders" / "a.hlsl"));
	CHECK(fs::exists(p.dir() / "Saved" / "Layout.json"));   // Saved/ はそのまま
	CHECK(fs::is_directory(p.dir() / "assets"));             // Assets/ を消してから、モデル用の assets/ を作る
	CHECK_FALSE(fs::exists(p.dir() / "assets" / "graphs"));
	{
		std::ifstream f(p.dir() / ".gitignore");
		std::string ignore((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		CHECK(ignore == "Saved/\nCache/\n");
	}

	// 書き直したプロジェクトファイルは version 2 として開ける。2 回目の移行はしない
	Project reopened;
	ProjectDiagnostics d2;
	REQUIRE(loadProject(file, reopened, d2));
	CHECK_FALSE(reopened.desc.legacyLayout());
	CHECK(reopened.assetRoot() == reopened.dir());
	CHECK_FALSE(migrateProjectLayout(reopened, moved, error));

	fs::remove_all(root);
}

TEST_CASE("プロジェクト: 直下に同じ名前があれば、何も動かさずに失敗する")
{
	const fs::path root = freshDir("studio_tests_project_conflict");
	const fs::path file = makeLegacyProject(root / "Old");
	writeText(root / "Old" / "shaders" / "mine.txt", "x");   // 直下にすでに shaders/

	Project p;
	ProjectDiagnostics diag;
	REQUIRE(loadProject(file, p, diag));
	std::vector<std::string> moved;
	std::string error;
	CHECK_FALSE(migrateProjectLayout(p, moved, error));
	CHECK(error.find("shaders") != std::string::npos);
	CHECK(moved.empty());
	CHECK(p.desc.legacyLayout());
	CHECK(fs::exists(root / "Old" / "Assets" / "graphs" / "viewport.json"));
	CHECK(fs::exists(root / "Old" / "Assets" / "shaders" / "a.hlsl"));

	fs::remove_all(root);
}
