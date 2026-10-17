// ============================================================
//  プロジェクトファイル (.sunsetproj) と最近のプロジェクトの単体テスト (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/Project.h"

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
	CHECK(back.assets == "Assets");
	CHECK(back.viewportGraph == "graphs/viewport.json");
	CHECK(diag.warnings.empty());

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
	CHECK(fs::exists(p.assetRoot() / "graphs" / "viewport.json"));
	CHECK(fs::exists(p.assetRoot() / "shaders" / "a.hlsl"));
	CHECK(fs::is_directory(p.savedDir()));
	CHECK(fs::exists(p.dir() / ".gitignore"));

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

	// アセットのフォルダがない / 壊れた JSON
	fs::remove_all(p.assetRoot());
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
