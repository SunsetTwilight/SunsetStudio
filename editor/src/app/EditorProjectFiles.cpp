// ============================================================
//  editor/src/app/EditorProjectFiles.cpp
//
//  プロジェクトに要るファイルと、ライトの種類。
//    light.types              ライトの種類 (lights/*.light.json。M5-5)
//    project.migrateMaterials マテリアル (M5-4) のころのプロジェクトを、マテリアルなしの描画に移す (2026-10-22)。
//                             移すものは project.info の materialMigration ({ changes, manual }) に出る
//  標準のライトの種類 (lights/) と、それが include する shaders/common.hlsli がないプロジェクトには、
//  開くときにテンプレートから足す (ensureProjectSupport。あるファイルは上書きしない)。
//  マテリアルの仕組み (materials/*.material.json、スロットへの割り当て、マテリアルのタブ) は 2026-10-22 に外した
//  (ノード側を充実させてから作り直す)。
// ============================================================
#include "EditorApp.h"

#include "services/AssetFiles.h"
#include "services/LightTypes.h"
#include "services/MaterialMigration.h"

#include "gfx/rendergraph/GraphAsset.h"

namespace studio
{
void EditorApp::ensureProjectSupport(const Project& project)
{
	// テンプレートの、ライトの仕組みに要るファイル (なければ足す。あるものは上書きしない)
	const std::filesystem::path tmpl = m_options.templateRoot / "deferred";
	const std::filesystem::path root = project.assetRoot();
	std::error_code ec;
	std::vector<std::string> added;
	auto copyIfMissing = [&](const std::filesystem::path& from, const std::string& rel) {
		const std::filesystem::path to = root / pathFromUtf8(rel);
		if (std::filesystem::exists(to, ec))
			return;
		std::filesystem::create_directories(to.parent_path(), ec);
		if (std::filesystem::copy_file(from, to, ec))
			added.push_back(rel);
	};
	for (const auto& e : std::filesystem::directory_iterator(tmpl / "lights", ec))
		if (e.is_regular_file(ec))
			copyIfMissing(e.path(), "lights/" + pathToUtf8(e.path().filename()));
	// ライトの種類の関数と影のシェーダーが include する共通の定数 (空のテンプレートにはない)
	copyIfMissing(tmpl / "shaders" / "common.hlsli", "shaders/common.hlsli");
	if (!added.empty())
	{
		std::string text = "ライトに要るファイルをテンプレートから足しました:";
		for (const auto& a : added) text += " " + a;
		log("info", "project", text);
	}
}

Json EditorApp::materialMigrationJson() const
{
	if (!m_project)
		return nullptr;
	const MaterialMigrationPlan plan = planMaterialRemoval(m_project->assetRoot(), m_project->desc.viewportGraph, m_options.templateRoot);
	if (!plan.needed() && plan.manual.empty())
		return nullptr;
	Json changes = Json::array();
	for (const auto& c : plan.changes)
		changes.push_back(c.what);
	return { { "changes", changes }, { "manual", plan.manual } };
}

void EditorApp::registerProjectFileMethods()
{
	Bridge& b = m_host.bridge();

	// ライトの種類 (M5-5): lights/*.light.json。関数のファイル (.light.hlsl) がなければ error
	b.registerMethod("light.types", [this](const Json&, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		const std::filesystem::path dir = m_project->assetRoot() / "lights";
		Json types = Json::array();
		for (const std::string& rel : listAssetFiles(m_project->assetRoot(), "lights", { ".json" }))
		{
			const std::string file = rel.substr(rel.rfind('/') + 1);
			const std::string tail = ".light.json";
			if (file.size() <= tail.size() || file.compare(file.size() - tail.size(), tail.size(), tail) != 0)
				continue;
			const std::string id = file.substr(0, file.size() - tail.size());
			LightTypeDef def;
			LightTypeDiagnostics diag;
			std::string text;
			Json json;
			bool ok = gfx::rg::readTextFile(m_project->assetRoot() / pathFromUtf8(rel), text);
			if (ok)
				try { json = Json::parse(text); } catch (const std::exception& e) { diag.errors.push_back(e.what()); ok = false; }
			ok = ok && lightTypeFromJson(id, json, def, diag);
			Json t = ok ? lightTypeToJson(def) : Json{ { "id", id }, { "label", id }, { "params", Json::array() } };
			std::error_code ec;
			if (!ok)
				t["error"] = diag.errors.empty() ? std::string("読めません") : diag.errors.front();
			else if (!std::filesystem::exists(dir / pathFromUtf8(id + ".light.hlsl"), ec))
				t["error"] = "lights/" + id + ".light.hlsl (Light_" + id + " の関数) がありません";
			types.push_back(std::move(t));
		}
		r.ok({ { "types", std::move(types) } });
	});

	// マテリアルのころのプロジェクトを移す (書いたファイルはホットリロードで描画に反映される)
	b.registerMethod("project.migrateMaterials", [this](const Json&, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		const MaterialMigrationPlan plan = planMaterialRemoval(m_project->assetRoot(), m_project->desc.viewportGraph, m_options.templateRoot);
		std::vector<std::string> written;
		std::string error;
		const bool ok = applyMaterialRemoval(m_project->assetRoot(), plan, written, error);
		std::string list;
		for (const auto& w : written) list += (list.empty() ? "" : ", ") + w;
		if (!written.empty())
			log("info", "project", "マテリアルなしの描画に移しました: " + list);
		for (const auto& m : plan.manual)
			log("warning", "project", "手で直してください: " + m);
		m_host.emit("project.changed", projectJson());
		if (!ok)
			return r.error(BridgeError::Internal, error);
		r.ok({ { "written", written }, { "manual", plan.manual } });
	});
}
} // namespace studio
