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
	fs::path r = (dir() / pathFromUtf8(desc.assets)).lexically_normal();
	// "." (version 2) だと末尾に区切りが残るので取る (比べたり、相対パスを作ったりするため)
	if (!r.has_filename() && r.has_parent_path() && r != r.root_path())
		r = r.parent_path();
	return r;
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
	if (d.legacyLayout())
		j["assets"] = d.assets;   // version 2 はプロジェクトのフォルダが起点なので書かない
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
	d.version = 1;   // version のないファイルは M4a の形
	bool hasAssets = false;
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
		else if (key == "assets") { str(v, "assets", d.assets); hasAssets = true; }
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
	if (d.version >= 2)
	{
		if (hasAssets && d.assets != ".")
			diag.warnings.push_back("assets: version 2 ではプロジェクトのフォルダが起点なので無視します");
		d.assets = ".";
	}
	else if (!hasAssets)
		d.assets = "Assets";
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

// Saved/ と Cache/ はエディターが作るもの (git で管理しない)
const char* k_GitIgnore = "Saved/\nCache/\n";

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

bool migrateProjectLayout(Project& project, std::vector<std::string>& moved, std::string& error)
{
	moved.clear();
	if (!project.desc.legacyLayout())
		return error = "すでに新しい構成です", false;
	std::error_code ec;
	const fs::path src = project.assetRoot();
	const fs::path dst = project.dir();
	if (src == dst || !fs::is_directory(src, ec))
		return error = "アセットのフォルダがありません: " + pathToUtf8(src), false;

	// 先に、直下に同じ名前のものがないかを全部確かめる (何も動かさずに失敗する)
	std::vector<fs::path> entries;
	std::vector<std::string> conflicts;
	for (const auto& e : fs::directory_iterator(src, ec))
	{
		entries.push_back(e.path());
		const fs::path target = dst / e.path().filename();
		if (fs::exists(target, ec) && !fs::equivalent(target, src, ec))
			conflicts.push_back(pathToUtf8(e.path().filename()));
	}
	if (ec)
		return error = "アセットのフォルダを読めません: " + ec.message(), false;
	if (!conflicts.empty())
	{
		error = "プロジェクトの直下に同じ名前のものがあるので移せません:";
		for (const auto& c : conflicts) error += " " + c;
		return false;
	}

	// 移す (失敗したら、移したものを戻す)
	std::vector<fs::path> done;
	for (const auto& from : entries)
	{
		const fs::path to = dst / from.filename();
		fs::rename(from, to, ec);
		if (ec)
		{
			error = "移せません: " + pathToUtf8(from.filename()) + " (" + ec.message() + ")";
			for (auto it = done.rbegin(); it != done.rend(); ++it)
			{
				std::error_code ec2;
				fs::rename(dst / it->filename(), *it, ec2);
			}
			return false;
		}
		done.push_back(from);
		moved.push_back(pathToUtf8(from.filename()));
	}
	fs::remove(src, ec);   // 空になった Assets/ (Windows では assets/ と同じ名前になるので、先に消す)
	if (ec)
		return error = "Assets/ を消せません (中身は移しました): " + ec.message(), false;
	for (const char* d : { "scenes", "graphs", "materials", "lights", "pipelines", "shaders", "assets" })
		fs::create_directories(dst / d, ec);

	ProjectDesc next = project.desc;
	next.version = k_ProjectVersion;
	next.assets  = ".";
	Project updated = project;
	updated.desc = next;
	if (!saveProject(updated, error))
		return false;
	// .gitignore に Cache/ を足す (なければ作る)
	std::string ignore;
	readText(dst / ".gitignore", ignore);
	if (ignore.find("Cache/") == std::string::npos)
	{
		if (!ignore.empty() && ignore.back() != '\n') ignore += "\n";
		ignore += ignore.find("Saved/") == std::string::npos ? k_GitIgnore : "Cache/\n";
		std::string e2;
		writeTextAtomic(dst / ".gitignore", ignore, e2);
	}
	project = std::move(updated);
	return true;
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
	// テンプレートの中身 (graphs/ pipelines/ shaders/ など) を、そのままプロジェクトの直下に置く (version 2)
	fs::copy(templateDir, dir, fs::copy_options::recursive, ec);
	if (ec)
	{
		error = "テンプレートをコピーできません: " + ec.message();
		return false;
	}
	for (const char* d : { "scenes", "graphs", "materials", "lights", "pipelines", "shaders", "assets" })
		fs::create_directories(dir / d, ec);
	if (!writeTextAtomic(dir / ".gitignore", k_GitIgnore, error))
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
