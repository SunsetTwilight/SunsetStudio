// ============================================================
//  editor/src/services/ProjectAssets.cpp
// ============================================================
#include "ProjectAssets.h"

#include "sunset/asset/model/ModelImporter.h"
#include "sunset/asset/texture/Texture.h"

#include <algorithm>

namespace studio
{
namespace fs = std::filesystem;
namespace sa = sunset::asset;

namespace
{
bool taken(const fs::path& p)
{
	std::error_code ec;
	return fs::exists(p, ec) || fs::exists(sa::metaPathFor(p), ec);
}

// dir/<stem><ext> が使われていれば dir/<stem>-2<ext>、-3 … (ファイルにも .meta にも重ならない名前)
fs::path uniqueName(const fs::path& dir, const std::string& stem, const std::string& ext)
{
	fs::path p = dir / sa::pathFromUtf8(stem + ext);
	for (int n = 2; taken(p); ++n)
		p = dir / sa::pathFromUtf8(stem + "-" + std::to_string(n) + ext);
	return p;
}

// ファイル名の末尾から、登録されている拡張子 (".tar.gz" のような 2 段も) を除いた名前と拡張子
std::pair<std::string, std::string> splitName(const fs::path& file, const sa::Importer& importer)
{
	const std::string name = sa::pathToUtf8(file.filename());
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	for (const std::string& ext : importer.extensions())
		if (lower.size() > ext.size() && lower.ends_with(ext))
			return { name.substr(0, name.size() - ext.size()), name.substr(name.size() - ext.size()) };
	return { sa::pathToUtf8(file.stem()), sa::pathToUtf8(file.extension()) };
}
} // namespace

std::optional<std::string> assetIdOfModel(const std::string& model)
{
	constexpr std::string_view prefix = "asset:";
	if (!model.starts_with(prefix) || model.size() != prefix.size() + 32)
		return std::nullopt;
	const std::string id = model.substr(prefix.size());
	if (!std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
		return std::nullopt;
	return id;
}

ProjectAssets::ProjectAssets()
{
	m_registry.add(sa::makeModelImporter());
	m_registry.add(sa::makeTextureImporter());
	m_db = std::make_unique<sa::AssetDatabase>(m_registry);
}

void ProjectAssets::open(const fs::path& projectDir, sa::Diagnostics& diag)
{
	m_root  = projectDir / "assets";
	m_cache = projectDir / "Cache" / "assets";
	std::error_code ec;
	fs::create_directories(m_root, ec);
	if (ec)
		diag.error("assets/ を作れません: " + ec.message());
	rescan(diag);
}

void ProjectAssets::close()
{
	m_db = std::make_unique<sa::AssetDatabase>(m_registry);
	m_root.clear();
	m_cache.clear();
}

sa::AssetDatabase::ScanResult ProjectAssets::rescan(sa::Diagnostics& diag)
{
	if (!isOpen())
		return {};
	return m_db->scan(m_root, diag);
}

const sa::AssetRecord* ProjectAssets::find(const std::string& id) const
{
	const std::optional<sa::Guid> g = sa::Guid::parse(id);
	return g ? m_db->find(*g) : nullptr;
}

nlohmann::json ProjectAssets::listJson() const
{
	std::vector<const sa::AssetRecord*> records;
	for (const sa::AssetRecord& r : m_db->records())
		records.push_back(&r);
	std::sort(records.begin(), records.end(), [](auto* a, auto* b) { return a->path < b->path; });
	nlohmann::json out = nlohmann::json::array();
	for (const sa::AssetRecord* r : records)
	{
		nlohmann::json j = {
			{ "id", r->id.toString() }, { "path", r->path }, { "importer", r->meta.importer },
			{ "external", r->meta.external() }, { "missing", r->missing },
			{ "cached", isOpen() && m_db->upToDate(*r, m_cache) },
		};
		if (r->meta.external())
			j["source"] = r->meta.source;
		out.push_back(std::move(j));
	}
	return out;
}

std::vector<std::string> ProjectAssets::extensions(const std::string& importer) const
{
	std::vector<std::string> out;
	for (const sa::Importer* imp : m_registry.all())
		if (importer.empty() || imp->name() == importer)
			for (const std::string& e : imp->extensions())
				out.push_back(e);
	return out;
}

bool ProjectAssets::importCopy(const fs::path& file, ImportResult& result, std::string& error)
{
	result = {};
	if (!isOpen())
		return error = "プロジェクトを開いていません", false;
	std::error_code ec;
	if (!fs::is_regular_file(file, ec))
		return error = "ファイルがありません: " + sa::pathToUtf8(file), false;
	const sa::Importer* imp = m_registry.forFile(file);
	if (!imp)
		return error = "取り込めない種類のファイルです: " + sa::pathToUtf8(file.filename()), false;

	// 参照しているファイル (モデルのテクスチャ、glTF の .bin)
	sa::Diagnostics refDiag;
	const std::vector<std::string> refs = imp->referencedFiles(file, refDiag);
	for (auto& w : refDiag.warnings) result.warnings.push_back(w);
	for (auto& e : refDiag.errors)   result.warnings.push_back(e);

	const auto [stem, ext] = splitName(file, *imp);
	// モデル (や参照のあるファイル) はフォルダにまとめる: assets/<名前>/<元のファイル名>
	fs::path dest;
	fs::path destDir = m_root;
	if (imp->name() == "model" || !refs.empty())
	{
		destDir = uniqueName(m_root, stem, "");
		dest    = destDir / file.filename();
	}
	else
		dest = uniqueName(m_root, stem, ext);
	fs::create_directories(destDir, ec);
	if (!fs::copy_file(file, dest, fs::copy_options::overwrite_existing, ec))
		return error = "コピーできません: " + sa::pathToUtf8(file) + " (" + ec.message() + ")", false;

	const fs::path srcDir = file.parent_path();
	for (const std::string& ref : refs)
	{
		const fs::path rel = sa::pathFromUtf8(ref).lexically_normal();
		if (rel.empty() || rel.is_absolute() || rel.has_root_name() || *rel.begin() == "..")
		{
			result.warnings.push_back("元のフォルダの外のファイルはコピーしません: " + ref);
			continue;
		}
		const fs::path from = srcDir / rel;
		if (!fs::is_regular_file(from, ec))
		{
			result.warnings.push_back("参照しているファイルがありません: " + ref);
			continue;
		}
		const fs::path to = destDir / rel;
		if (fs::exists(to, ec))
			continue;   // 同じファイルを 2 回参照している
		fs::create_directories(to.parent_path(), ec);
		if (!fs::copy_file(from, to, ec))
		{
			result.warnings.push_back("コピーできません: " + ref + " (" + ec.message() + ")");
			continue;
		}
		result.copied.push_back(sa::genericUtf8(to.lexically_relative(m_root)));
	}

	sa::Diagnostics diag;
	rescan(diag);
	for (auto& w : diag.warnings) result.warnings.push_back(w);
	result.path = sa::genericUtf8(dest.lexically_relative(m_root));
	const sa::AssetRecord* r = m_db->findByPath(result.path);
	if (!r)
		return error = "取り込んだファイルが一覧にありません: " + result.path, false;
	result.id = r->id.toString();
	return true;
}

bool ProjectAssets::importLink(const fs::path& file, ImportResult& result, std::string& error)
{
	result = {};
	if (!isOpen())
		return error = "プロジェクトを開いていません", false;
	const sa::Importer* imp = m_registry.forFile(file);
	if (!imp)
		return error = "取り込めない種類のファイルです: " + sa::pathToUtf8(file.filename()), false;
	const auto [stem, ext] = splitName(file, *imp);
	const std::string metaName = sa::genericUtf8(uniqueName(m_root, stem, ext).lexically_relative(m_root));
	sa::Guid id;
	if (!m_db->linkExternal(file, metaName, id, error))
		return false;
	sa::Diagnostics diag;
	rescan(diag);
	for (auto& w : diag.warnings) result.warnings.push_back(w);
	result.id   = id.toString();
	result.path = metaName;
	return true;
}

std::shared_ptr<const sa::Model> ProjectAssets::loadModel(const sa::AssetRecord& record, const fs::path& cacheDir, sa::Diagnostics& diag) const
{
	if (record.meta.importer != "model")
		return diag.error(record.path + ": モデルではありません (" + record.meta.importer + ")"), nullptr;
	// import はデータベースの表を使わないので、別の入れ物で呼ぶ (UI スレッドの scan と同時でもよい)
	const sa::AssetDatabase db(m_registry);
	fs::path artifact;
	if (!db.import(record, cacheDir, artifact, diag))
		return nullptr;
	auto model = std::make_shared<sa::Model>();
	std::string error;
	if (!sa::loadModel(artifact, *model, &error))
		return diag.error(record.path + ": キャッシュを読めません: " + error), nullptr;
	return model;
}
} // namespace studio
