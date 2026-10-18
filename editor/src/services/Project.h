#pragma once
// ============================================================
//  editor/src/services/Project.h
//
//  プロジェクトファイル (.sunsetproj) と、最近のプロジェクトの一覧 (M4a)。
//  設計: docs/design/editor-shell.md の「プロジェクトファイル」。GPU も webview も使わないので単体テストできる。
//
//  構成 (version 2、M5 から。アセットの起点はプロジェクトのフォルダ):
//    MyWork/
//    ├── MyWork.sunsetproj      { "version": 2, "name": ..., "engine": ...,
//    │                            "viewport": { "graph": "graphs/viewport.json", "scene": "scenes/main.scene.json" } }
//    ├── scenes/ graphs/ materials/ lights/ pipelines/ shaders/   種類ごとのアセット
//    ├── assets/                モデルとテクスチャ (中は自由に整理する)
//    ├── Saved/                 エディターが作るもの (パネルの配置など。git で管理しない)
//    └── Cache/                 作り直せるもの (変換したモデルなど。git で管理しない)
//
//  version 1 (M4a) はアセットを Assets/ (キー "assets") の下に置いた。そのまま開けて、
//  migrateProjectLayout で Assets/ の中身を直下へ移すと version 2 になる。
// ============================================================

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

constexpr const char* k_ProjectExtension = ".sunsetproj";
constexpr int         k_ProjectVersion   = 2;   // 2: アセットの起点がプロジェクトのフォルダ (1: Assets/)

struct ProjectDesc
{
	int         version       = k_ProjectVersion;
	std::string name;
	std::string engine;                                  // 作ったときのエンジンのバージョン ("0.3")
	std::string assets        = ".";                     // アセットの起点 (プロジェクトファイルからの相対パス)。version 1 では "Assets"
	std::string viewportGraph = "graphs/viewport.json";  // アセットの起点からの相対パス
	std::string viewportScene = "scenes/main.scene.json"; // 同上 (M5-2。なければ既定のシーンを作る)

	bool legacyLayout() const { return version < 2; }   // Assets/ の下に置く古い構成
};

// 開いたプロジェクト (パスはすべて絶対パス)
struct Project
{
	std::filesystem::path file;   // MyGame/MyGame.sunsetproj
	ProjectDesc           desc;

	std::filesystem::path dir() const       { return file.parent_path(); }
	std::filesystem::path assetRoot() const;
	std::filesystem::path savedDir() const  { return dir() / "Saved"; }
	std::filesystem::path layoutFile() const { return savedDir() / "Layout.json"; }
};

struct ProjectDiagnostics
{
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
};

// UTF-8 の文字列とパスの変換
std::filesystem::path pathFromUtf8(const std::string& s);
std::string           pathToUtf8(const std::filesystem::path& p);

Json projectToJson(const ProjectDesc& desc);
bool projectFromJson(const Json& json, ProjectDesc& out, ProjectDiagnostics& diag);

// ファイルの読み書き。読めない / 形が違えば false (diag に理由)。
// currentEngine を渡すと、プロジェクトの engine のほうが新しければ警告する
bool loadProject(const std::filesystem::path& file, Project& out, ProjectDiagnostics& diag, const std::string& currentEngine = {});
bool saveProject(const Project& project, std::string& error);

// テンプレートのフォルダ (中身がそのまま Assets/ になる) から、dir/<name>/<name>.sunsetproj を作る。
// dir/<name> がすでにあって空でなければ失敗する
bool createProject(const std::filesystem::path& parentDir, const std::string& name, const std::filesystem::path& templateDir,
                   const std::string& engineVersion, Project& out, std::string& error);

// version 1 (Assets/) のプロジェクトを version 2 に移す: Assets/ の中身をプロジェクトの直下へ移し、
// Assets/ を消して、プロジェクトファイルを書き直す。直下に同じ名前のものがあれば、何も動かさずに失敗する。
// moved に移したもの (名前) を入れる。途中で失敗したら、移したものを戻す
bool migrateProjectLayout(Project& project, std::vector<std::string>& moved, std::string& error);

// プロジェクトの名前として使えるか (ファイル名に使えない文字、空、長すぎる)
bool isValidProjectName(const std::string& name, std::string* why = nullptr);

// 最近のプロジェクト (新しい順、最大 maxCount 件)。保存先は file (%LOCALAPPDATA%/SunsetStudio/recent.json)
class RecentProjects
{
public:
	explicit RecentProjects(std::filesystem::path file, size_t maxCount = 12) : m_file(std::move(file)), m_max(maxCount) {}

	void load();
	bool save() const;
	void add(const Project& project);              // 先頭に入れる (同じファイルは前のものを消す)
	void remove(const std::filesystem::path& file);

	struct Entry
	{
		std::filesystem::path file;
		std::string           name;
		bool                  exists = true;        // load のときにファイルがあったか
	};
	const std::vector<Entry>& entries() const { return m_entries; }
	static std::filesystem::path defaultFile();   // %LOCALAPPDATA%/SunsetStudio/recent.json

private:
	std::filesystem::path m_file;
	size_t                m_max;
	std::vector<Entry>    m_entries;
};
} // namespace studio
