// ============================================================
//  editor/src/services/Project.cpp
// ============================================================
#include "Project.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace studio
{
namespace fs = std::filesystem;

fs::path pathFromUtf8(const std::string& s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

std::string pathToUtf8(const fs::path& p)
{
	const std::u8string u = p.u8string();
	return std::string(u.begin(), u.end());
}

fs::path Project::assetRoot() const
{
	return (dir() / pathFromUtf8(desc.assets)).lexically_normal();
}

// ============================================================
//  JSON
// ============================================================
Json projectToJson(const ProjectDesc& d)
{
	Json j = Json::object();
	j["version"] = d.version;
	j["name"]    = d.name;
	if (!d.engine.empty())
		j["engine"] = d.engine;
	j["assets"]   = d.assets;
	j["viewport"] = { { "graph", d.viewportGraph } };
	return j;
}

bool projectFromJson(const Json& json, ProjectDesc& out, ProjectDiagnostics& diag)
{
	const size_t errorsBefore = diag.errors.size();
	if (!json.is_object())
	{
		diag.errors.push_back("(root): オブジェクトが必要です");
		return false;
	}
	ProjectDesc d;
	auto str = [&](const Json& v, const std::string& path, std::string& dst) {
		if (v.is_string()) dst = v.get<std::string>();
		else diag.errors.push_back(path + ": 文字列が必要です");
	};
	for (const auto& [key, v] : json.items())
	{
		if (key == "version")
		{
			if (!v.is_number_integer()) diag.errors.push_back("version: 整数が必要です");
			else
			{
				d.version = v.get<int>();
				if (d.version > k_ProjectVersion)
					diag.warnings.push_back("version: 新しい形式のプロジェクトです。知らない項目は無視します");
			}
		}
		else if (key == "name")   str(v, "name", d.name);
		else if (key == "engine") str(v, "engine", d.engine);
		else if (key == "assets") str(v, "assets", d.assets);
		else if (key == "viewport")
		{
			if (!v.is_object()) { diag.errors.push_back("viewport: オブジェクトが必要です"); continue; }
			for (const auto& [k2, v2] : v.items())
			{
				if (k2 == "graph") str(v2, "viewport.graph", d.viewportGraph);
				else diag.warnings.push_back("viewport." + k2 + ": 知らないキーなので無視します");
			}
		}
		else
			diag.warnings.push_back(key + ": 知らないキーなので無視します");
	}
	if (d.assets.empty())
		diag.errors.push_back("assets: アセットのフォルダが空です");
	if (pathFromUtf8(d.assets).is_absolute())
		diag.errors.push_back("assets: プロジェクトファイルからの相対パスにしてください");
	if (d.viewportGraph.empty())
		diag.errors.push_back("viewport.graph: ビューポートのグラフが空です");
	if (diag.errors.size() != errorsBefore)
		return false;
	out = std::move(d);
	return true;
}

// ============================================================
//  ファイル
// ============================================================
namespace
{
bool readText(const fs::path& p, std::string& out)
{
	std::ifstream f(p, std::ios::binary);
	if (!f) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

bool writeTextAtomic(const fs::path& p, const std::string& text, std::string& error)
{
	std::error_code ec;
	if (p.has_parent_path())
		fs::create_directories(p.parent_path(), ec);
	fs::path tmp = p;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f || !(f << text))
		{
			error = "書き込めません: " + pathToUtf8(tmp);
			return false;
		}
	}
	fs::rename(tmp, p, ec);
	if (ec)
	{
		fs::remove(tmp, ec);
		error = "置き換えに失敗しました: " + pathToUtf8(p);
		return false;
	}
	return true;
}

// "0.3" と "0.10" を数値として比べる (a > b なら正)
int compareVersion(const std::string& a, const std::string& b)
{
	std::istringstream sa(a), sb(b);
	for (;;)
	{
		std::string pa, pb;
		const bool ha = static_cast<bool>(std::getline(sa, pa, '.'));
		const bool hb = static_cast<bool>(std::getline(sb, pb, '.'));
		if (!ha && !hb) return 0;
		const long va = ha ? std::strtol(pa.c_str(), nullptr, 10) : 0;
		const long vb = hb ? std::strtol(pb.c_str(), nullptr, 10) : 0;
		if (va != vb) return va > vb ? 1 : -1;
	}
}
} // namespace

bool loadProject(const fs::path& file, Project& out, ProjectDiagnostics& diag, const std::string& currentEngine)
{
	std::string text;
	if (!readText(file, text))
	{
		diag.errors.push_back("プロジェクトファイルを読めません: " + pathToUtf8(file));
		return false;
	}
	Json json;
	try
	{
		json = Json::parse(text);
	}
	catch (const std::exception& e)
	{
		diag.errors.push_back(std::string("JSON: ") + e.what());
		return false;
	}
	ProjectDesc desc;
	if (!projectFromJson(json, desc, diag))
		return false;
	if (desc.name.empty())
		desc.name = pathToUtf8(file.stem());
	if (!currentEngine.empty() && !desc.engine.empty() && compareVersion(desc.engine, currentEngine) > 0)
		diag.warnings.push_back("engine: プロジェクトはエンジン " + desc.engine + " で作られています (このエディターは " + currentEngine + ")");

	Project p;
	p.file = fs::absolute(file).lexically_normal();
	p.desc = std::move(desc);
	std::error_code ec;
	if (!fs::is_directory(p.assetRoot(), ec))
	{
		diag.errors.push_back("assets: フォルダがありません: " + pathToUtf8(p.assetRoot()));
		return false;
	}
	out = std::move(p);
	return true;
}

bool saveProject(const Project& project, std::string& error)
{
	return writeTextAtomic(project.file, projectToJson(project.desc).dump(2) + "\n", error);
}

bool isValidProjectName(const std::string& name, std::string* why)
{
	auto fail = [&](const char* w) { if (why) *why = w; return false; };
	if (name.empty())        return fail("名前が空です");
	if (name.size() > 100)   return fail("名前が長すぎます (100 バイトまで)");
	if (name.front() == ' ' || name.back() == ' ' || name.back() == '.')
		return fail("名前の先頭と末尾に空白、末尾に . は使えません");
	for (unsigned char c : name)
		if (c < 0x20 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos)
			return fail("名前に < > : \" / \\ | ? * と制御文字は使えません");
	static const char* reserved[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3" };
	for (const char* r : reserved)
		if (_stricmp(name.c_str(), r) == 0)
			return fail("Windows で予約された名前は使えません");
	return true;
}

bool createProject(const fs::path& parentDir, const std::string& name, const fs::path& templateDir,
                   const std::string& engineVersion, Project& out, std::string& error)
{
	std::string why;
	if (!isValidProjectName(name, &why))
	{
		error = "プロジェクトの名前: " + why;
		return false;
	}
	std::error_code ec;
	if (!fs::is_directory(templateDir, ec))
	{
		error = "テンプレートがありません: " + pathToUtf8(templateDir);
		return false;
	}
	const fs::path dir = fs::absolute(parentDir / pathFromUtf8(name)).lexically_normal();
	if (fs::exists(dir, ec) && !fs::is_empty(dir, ec))
	{
		error = "フォルダがすでにあり、空ではありません: " + pathToUtf8(dir);
		return false;
	}
	fs::create_directories(dir / "Saved", ec);
	if (ec)
	{
		error = "フォルダを作れません: " + pathToUtf8(dir) + " (" + ec.message() + ")";
		return false;
	}
	fs::copy(templateDir, dir / "Assets", fs::copy_options::recursive, ec);
	if (ec)
	{
		error = "テンプレートをコピーできません: " + ec.message();
		return false;
	}
	// Saved/ はエディターが作るもの (git で管理しない)
	if (!writeTextAtomic(dir / ".gitignore", "Saved/\n", error))
		return false;

	Project p;
	p.file          = dir / pathFromUtf8(name + k_ProjectExtension);
	p.desc.name     = name;
	p.desc.engine   = engineVersion;
	if (!saveProject(p, error))
		return false;
	out = std::move(p);
	return true;
}

// ============================================================
//  最近のプロジェクト
// ============================================================
fs::path RecentProjects::defaultFile()
{
	wchar_t* value = nullptr;
	size_t   len   = 0;
	fs::path base;
	if (_wdupenv_s(&value, &len, L"LOCALAPPDATA") == 0 && value)
	{
		base = fs::path(value);
		free(value);
	}
	if (base.empty())
		base = fs::temp_directory_path();
	return base / "SunsetStudio" / "recent.json";
}

void RecentProjects::load()
{
	m_entries.clear();
	std::string text;
	if (!readText(m_file, text))
		return;
	try
	{
		const Json j = Json::parse(text);
		for (const auto& e : j.value("projects", Json::array()))
		{
			Entry entry;
			entry.file = pathFromUtf8(e.value("file", std::string()));
			entry.name = e.value("name", std::string());
			if (entry.file.empty())
				continue;
			std::error_code ec;
			entry.exists = fs::is_regular_file(entry.file, ec);
			m_entries.push_back(std::move(entry));
			if (m_entries.size() >= m_max)
				break;
		}
	}
	catch (const std::exception&)
	{
		m_entries.clear();   // 壊れていたら空から始める
	}
}

bool RecentProjects::save() const
{
	Json list = Json::array();
	for (const auto& e : m_entries)
		list.push_back({ { "file", pathToUtf8(e.file) }, { "name", e.name } });
	std::string error;
	return writeTextAtomic(m_file, Json{ { "version", 1 }, { "projects", list } }.dump(2) + "\n", error);
}

void RecentProjects::add(const Project& project)
{
	remove(project.file);
	m_entries.insert(m_entries.begin(), Entry{ project.file, project.desc.name, true });
	if (m_entries.size() > m_max)
		m_entries.resize(m_max);
}

void RecentProjects::remove(const fs::path& file)
{
	const fs::path norm = fs::absolute(file).lexically_normal();
	m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
	                               [&](const Entry& e) { return fs::absolute(e.file).lexically_normal() == norm; }),
	                m_entries.end());
}
} // namespace studio
