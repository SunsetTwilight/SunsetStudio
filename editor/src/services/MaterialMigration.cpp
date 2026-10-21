// ============================================================
//  editor/src/services/MaterialMigration.cpp
// ============================================================
#include "MaterialMigration.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace studio
{
namespace fs = std::filesystem;
using Json   = nlohmann::json;

namespace
{
const char* k_OldShadowPipeline = "pipelines/materials/standard.shadow.json";
const char* k_NewShadowPipeline = "pipelines/shadow.json";

fs::path pathOf(const fs::path& root, const std::string& rel)
{
	return root / fs::path(std::u8string(rel.begin(), rel.end()));
}

bool readText(const fs::path& file, std::string& out)
{
	std::ifstream f(file, std::ios::binary);
	if (!f)
		return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

} // namespace

MaterialMigrationPlan planMaterialRemoval(const fs::path& root, const std::string& graphFile, const fs::path& templateRoot)
{
	MaterialMigrationPlan plan;
	std::string graphText;
	if (!readText(pathOf(root, graphFile), graphText))
		return plan;
	Json graph;
	try { graph = Json::parse(graphText); } catch (...) { return plan; }

	// ---- 影のパス ----
	bool oldShadow = false;
	for (const auto& p : graph.value("passes", Json::array()))
		if (p.value("pipeline", "") == k_OldShadowPipeline)
			oldShadow = true;
	if (oldShadow)
	{
		std::string next = graphText;
		const std::string from = std::string("\"") + k_OldShadowPipeline + "\"";
		const std::string to   = std::string("\"") + k_NewShadowPipeline + "\"";
		for (size_t at = next.find(from); at != std::string::npos; at = next.find(from, at + to.size()))
			next.replace(at, from.size(), to);
		plan.changes.push_back({ graphFile, next, std::string("影のパスを ") + k_NewShadowPipeline + " で描く (" + graphFile + ")" });
		// 影のファイル (なければテンプレートから)
		std::error_code ec;
		for (const char* rel : { "pipelines/shadow.json", "shaders/shadow.vs.hlsl", "shaders/shadow.ps.hlsl", "shaders/common.hlsli" })
		{
			if (fs::exists(pathOf(root, rel), ec))
				continue;
			std::string text;
			if (readText(templateRoot / "deferred" / fs::path(std::u8string(rel, rel + std::char_traits<char>::length(rel))), text))
				plan.changes.push_back({ rel, text, std::string("テンプレートから足す: ") + rel });
		}
	}

	return plan;
}

bool applyMaterialRemoval(const fs::path& root, const MaterialMigrationPlan& plan, std::vector<std::string>& written, std::string& error)
{
	for (const auto& c : plan.changes)
	{
		const fs::path file = pathOf(root, c.file);
		std::error_code ec;
		fs::create_directories(file.parent_path(), ec);
		std::ofstream f(file, std::ios::binary | std::ios::trunc);
		if (!f || !(f << c.text))
		{
			error = "書けません: " + c.file;
			return false;
		}
		written.push_back(c.file);
	}
	return true;
}
} // namespace studio
