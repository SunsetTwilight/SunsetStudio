// ============================================================
//  editor/src/app/EditorMaterials.cpp
//
//  マテリアル (M5-4): materials/*.material.json の一覧、説明 (リフレクションから作る欄)、編集、作成。
//    material.list      マテリアルのファイルの一覧
//    material.describe  キー (ファイルのパス / 取り込んだ値の "auto:…") のマテリアルと、パスごとの
//                       cbuffer Material の変数とテクスチャの変数 (描画スレッドのリフレクションから)
//    material.set       ファイルを書き換える (元に戻す / やり直すの対象。保存はすぐ。描画にもすぐ反映する)
//    material.create    新しいファイルを作る (取り込んだ値のマテリアルの中身を渡すと「マテリアルにする」になる)
//    light.types        ライトの種類 (lights/*.light.json。M5-5)
//  イベント: material.changed (ファイルが変わった)
//  標準のマテリアル (pipelines/materials/standard.gbuffer.json とシェーダー) と標準のライトの種類 (lights/、M5-5) が
//  ないプロジェクトには、開くときにテンプレートから足す (ensureMaterialSupport。あるファイルは上書きしない)。
// ============================================================
#include "EditorApp.h"

#include "services/AssetFiles.h"
#include "services/LightTypes.h"

#include "gfx/rendergraph/GraphAsset.h"

#include <algorithm>

namespace studio
{
namespace
{
bool isMaterialFile(const std::string& path)
{
	const std::string ext = ".material.json";
	return path.size() > ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}
} // namespace

void EditorApp::ensureMaterialSupport(const Project& project)
{
	// テンプレートの、マテリアルの仕組みに要るファイル (なければ足す。あるものは上書きしない)
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
	for (const char* dir : { "pipelines/materials", "shaders/materials", "lights" })   // lights: M5-5 のライトの種類
	{
		for (const auto& e : std::filesystem::directory_iterator(tmpl / pathFromUtf8(dir), ec))
			if (e.is_regular_file(ec))
				copyIfMissing(e.path(), std::string(dir) + "/" + pathToUtf8(e.path().filename()));
	}
	// shaders/materials/*.hlsli が include する共通の定数 (空のテンプレートにはない)
	copyIfMissing(tmpl / "shaders" / "common.hlsli", "shaders/common.hlsli");
	std::filesystem::create_directories(root / "materials", ec);
	if (!added.empty())
	{
		std::string text = "マテリアル / ライトに要るファイルをテンプレートから足しました:";
		for (const auto& a : added) text += " " + a;
		log("info", "material", text);
	}
}

// ファイルを書く (検証、書き込み、描画への反映、イベント)。material.set / create と、元に戻す / やり直すから
bool EditorApp::writeMaterialFile(const std::string& path, const Json& material, bool create, std::string& error)
{
	if (!m_project)
		return error = "プロジェクトを開いていません", false;
	if (!isMaterialFile(path) || path.rfind("materials/", 0) != 0)
		return error = "マテリアルのファイルは materials/ の下の *.material.json です: " + path, false;
	MaterialDesc desc;
	MaterialDiagnostics diag;
	if (!materialFromJson(material, desc, diag))
	{
		error = "マテリアルが正しくありません";
		for (const auto& e : diag.errors) error += "\n" + e;
		return false;
	}
	std::filesystem::path file;
	if (!resolveAssetPath(m_project->assetRoot(), path, file, error))
		return false;
	std::error_code ec;
	if (create && std::filesystem::exists(file, ec))
		return error = "同じ名前のファイルがあります: " + path, false;
	if (!create && !std::filesystem::exists(file, ec))
		return error = "ファイルがありません: " + path, false;
	std::filesystem::create_directories(file.parent_path(), ec);
	if (!gfx::rg::writeTextFileAtomic(file, materialToJson(desc).dump(2) + "\n", &error))
		return false;
	m_render.postScene([path](GraphicsEngine&, ViewportRenderer* scene) {
		if (scene) scene->materials().reload(path);
	});
	m_host.emit("material.changed", { { "path", path } });
	return true;
}

void EditorApp::registerMaterialMethods()
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

	b.registerMethod("material.list", [this](const Json&, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		std::vector<std::string> files;
		for (auto& f : listAssetFiles(m_project->assetRoot(), "materials", { ".json" }))
			if (isMaterialFile(f))
				files.push_back(std::move(f));
		std::sort(files.begin(), files.end());
		r.ok({ { "files", files } });
	});

	b.registerMethod("material.describe", [this](const Json& p, Bridge::Reply r) {
		const std::string key = p.value("key", std::string());
		if (key.empty())
			return r.error(BridgeError::InvalidParams, "key がありません");
		m_render.postScene([r, key](GraphicsEngine& engine, ViewportRenderer* scene) {
			if (!scene)
				return r.error(BridgeError::Unavailable, "ビューポートのシーンがありません");
			r.ok(scene->materials().describe(engine, key));
		});
	});

	auto write = [this](const std::string& path, const Json& material, bool create, std::string& error) {
		return writeMaterialFile(path, material, create, error);
	};

	b.registerMethod("material.set", [this, write](const Json& p, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		const std::string path = p.value("path", std::string());
		std::filesystem::path file;
		std::string error;
		if (!resolveAssetPath(m_project->assetRoot(), path, file, error))
			return r.error(BridgeError::InvalidParams, error);
		std::string beforeText;
		Json before;
		if (gfx::rg::readTextFile(file, beforeText))
			try { before = Json::parse(beforeText); } catch (...) {}
		const Json after = p.value("material", Json());
		if (!write(path, after, false, error))
			return r.error(BridgeError::InvalidParams, error);
		if (!before.is_null() && before != after)
			recordEdit({ p.value("label", std::string("マテリアルを編集")), "material", path, before, after });
		r.ok({ { "path", path } });
	});

	b.registerMethod("material.create", [this, write](const Json& p, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		const std::string path = p.value("path", std::string());
		Json material = p.value("material", Json());
		if (material.is_null())   // 中身がなければ、白の標準のマテリアル
			material = materialToJson(ViewportRenderer::standardMaterial({ 1, 1, 1, 1 }, 0.0f, 0.6f));
		std::string error;
		if (!write(path, material, true, error))
			return r.error(BridgeError::InvalidParams, error);
		log("info", "material", "マテリアルを作りました: " + path);
		r.ok({ { "path", path } });
	});
}
} // namespace studio
