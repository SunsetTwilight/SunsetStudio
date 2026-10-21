// ============================================================
//  editor/src/services/MaterialMigration.cpp
// ============================================================
#include "MaterialMigration.h"

#include "AssetFiles.h"

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

// 比べるための正規化 (改行を LF に、行末の空白と前後の空行を除く)
std::string normalized(const std::string& s)
{
	std::string out;
	std::string line;
	auto flush = [&] {
		const size_t end = line.find_last_not_of(" \t\r");
		out += (end == std::string::npos ? std::string() : line.substr(0, end + 1)) + "\n";
		line.clear();
	};
	for (char c : s)
	{
		if (c == '\n')
			flush();
		else
			line += c;
	}
	if (!line.empty())
		flush();
	const size_t b = out.find_first_not_of('\n');
	const size_t e = out.find_last_not_of('\n');
	return b == std::string::npos ? std::string() : out.substr(b, e - b + 1);
}

// レイアウトに b2 の CBV か記述子テーブル (マテリアルの定数 / テクスチャ) がある
bool usesMaterialBindings(const Json& pipeline)
{
	const Json* params = pipeline.contains("layout") ? &pipeline["layout"]["parameters"] : nullptr;
	if (!params || !params->is_array())
		return false;
	for (const auto& p : *params)
	{
		const std::string type = p.value("parameterType", "");
		if (type == "DescriptorTable" || (type == "CBV" && p.value("shaderRegister", 0) == 2))
			return true;
	}
	return false;
}

bool hasChange(const MaterialMigrationPlan& plan, const std::string& file)
{
	for (const auto& c : plan.changes)
		if (c.file == file)
			return true;
	return false;
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

	// ---- 描画ノード (drawList のパス。影とグリッドを除く) ----
	for (const auto& p : graph.value("passes", Json::array()))
	{
		const Json content = p.value("content", Json::object());
		if (!content.contains("drawList"))
			continue;
		const std::string layer = content["drawList"].value("layer", "");
		const std::string pipe  = p.value("pipeline", "");
		if (layer == "Shadow" || layer == "Grid" || pipe.empty() || pipe == k_OldShadowPipeline || hasChange(plan, pipe))
			continue;
		std::string pipeText;
		if (!readText(pathOf(root, pipe), pipeText))
			continue;
		Json pj;
		try { pj = Json::parse(pipeText); } catch (...) { continue; }
		if (!usesMaterialBindings(pj))
			continue;
		const std::string vs = pj["shaders"].value("vs", Json::object()).value("file", "");
		const std::string ps = pj["shaders"].value("ps", Json::object()).value("file", "");
		const bool depth = !(pj.contains("state") && pj["state"].contains("depthStencilState") &&
		                     pj["state"]["depthStencilState"].value("depthEnable", true) == false);
		if (normalized(pipeText) == normalized(legacyDrawPipeline(vs, ps, depth)))
			plan.changes.push_back({ pipe, drawPipelineText(vs, ps, depth), "描画ノード " + p.value("name", "") + " のパイプラインを、マテリアルなしの既定に置き換える (" + pipe + ")" });
		else
			plan.manual.push_back(pipe + " (" + p.value("name", "") + "): マテリアルの定数 (b2) かテクスチャのテーブルを使うレイアウトです。物体 (b0) とフレーム (b1) だけにしてください");
		std::string psText;
		if (ps.empty() || hasChange(plan, ps) || !readText(pathOf(root, ps), psText))
			continue;
		if (normalized(psText) == normalized(legacyDrawPixelShader()))
			plan.changes.push_back({ ps, drawPixelShader(), "描画ノード " + p.value("name", "") + " の PS を、マテリアルなしの既定に置き換える (" + ps + ")" });
		else if (psText.find("register(b2)") != std::string::npos || psText.find("register(t0)") != std::string::npos)
			plan.manual.push_back(ps + " (" + p.value("name", "") + "): cbuffer Material (b2) かテクスチャ (t0) を使っています。物体の色 (gColor) を使う形に直してください");
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
