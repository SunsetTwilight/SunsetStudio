#pragma once
// ============================================================
//  editor/src/services/Project.h
//
//  プロジェクトファイル (.sunsetproj) と、最近のプロジェクトの一覧 (M4a)。
//  設計: docs/design/editor-shell.md の「プロジェクトファイル」。GPU も webview も使わないので単体テストできる。
//
//    MyGame/
//    ├── MyGame.sunsetproj      { "version": 1, "name": ..., "engine": ..., "assets": "Assets",
//    │                            "viewport": { "graph": "graphs/viewport.json" } }
//    ├── Assets/                作品のアセット (graphs/ pipelines/ shaders/)
//    └── Saved/                 エディターが作るもの (パネルの配置など。git で管理しない)
// ============================================================

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

constexpr const char* k_ProjectExtension = ".sunsetproj";
constexpr int         k_ProjectVersion   = 1;

struct ProjectDesc
{
	int         version       = k_ProjectVersion;
	std::string name;
	std::string engine;                                  // 作ったときのエンジンのバージョン ("0.3")
	std::string assets        = "Assets";                // プロジェクトファイルからの相対パス
	std::string viewportGraph = "graphs/viewport.json";  // assets からの相対パス
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
