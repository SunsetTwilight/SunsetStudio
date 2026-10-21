// ============================================================
//  editor/src/app/EditorLibrary.cpp
//
//  アセットのライブラリ (M5-3): プロジェクトの assets/ を SunsetAsset で管理し、モデルをシーンに置けるようにする。
//    library.list      アセットの一覧 (ID、パス、種類、外部の参照か、キャッシュが新しいか、モデルの読み込みの状態)
//    library.import    ファイルを取り込む (mode: "copy" = assets/ にコピー / "link" = 外部のファイルを参照)。
//                      file がなければファイルの選択ダイアログを開く
//    library.refresh   assets/ を走査し直し、元のファイルが変わったモデルを読み直す
//    library.reimport  キャッシュを消して取り込み直す (.meta の設定を変えたあとなど)
//    library.load      モデルを (まだなら) 読む。シーンに置く前に境界を知るため、情報を見るため
//  イベント: library.changed
//  モデルの取り込みと読み込みは別のスレッド (m_libraryWorker) で行い、結果を描画スレッドの ViewportRenderer に渡す。
//  シーンが "asset:<ID>" のモデルを使うと、まだ読んでいなければ読む (requestSceneModels)。
// ============================================================
#include "EditorApp.h"

#include "host/FileDialog.h"

#include "sunset/asset/model/Model.h"

#include <cmath>

namespace studio
{
namespace sa = sunset::asset;

namespace
{
Json modelInfo(const sa::Model& m)
{
	size_t vertices = 0, triangles = 0, morphs = 0;
	for (const sa::ModelMesh& mesh : m.meshes)
	{
		vertices  += mesh.vertexCount;
		triangles += mesh.indices.size() / 3;
		morphs    += mesh.morphs.size();
	}
	return {
		{ "nodes", m.nodes.size() }, { "meshes", m.meshes.size() }, { "vertices", vertices }, { "triangles", triangles },
		{ "slots", m.slots.size() }, { "skins", m.skins.size() }, { "morphs", morphs },
		{ "boundsMin", m.boundsMin }, { "boundsMax", m.boundsMax },
		{ "slotNames", [&] { Json a = Json::array(); for (const auto& s : m.slots) a.push_back(s.name); return a; }() },
	};
}
} // namespace

void EditorApp::openLibrary()
{
	++m_libraryGeneration;
	m_modelStates.clear();
	m_library.close();
	if (m_project)
	{
		sa::Diagnostics diag;
		m_library.open(m_project->dir(), diag);
		for (const auto& w : diag.warnings) log("warning", "library", w);
		for (const auto& e : diag.errors)   log("error", "library", e);
	}
	m_host.emit("library.changed", Json::object());
}

Json EditorApp::libraryJson() const
{
	Json assets = m_library.listJson();
	for (Json& a : assets)
	{
		auto it = m_modelStates.find(a.at("id").get<std::string>());
		if (it == m_modelStates.end())
			continue;
		Json m = { { "status", it->second.status } };
		if (!it->second.error.empty()) m["error"] = it->second.error;
		if (!it->second.info.is_null()) m["info"] = it->second.info;
		a["model"] = std::move(m);
	}
	return { { "assets", std::move(assets) }, { "extensions", m_library.extensions() } };
}

void EditorApp::requestSceneModels()
{
	for (const SceneObject& o : m_sceneData.objects)
		if (const auto id = assetIdOfModel(o.model); id && !m_modelStates.contains(*id))
			requestModel(*id);
}

void EditorApp::requestModel(const std::string& id)
{
	ModelState& st = m_modelStates[id];
	const uint64_t ticket = ++st.ticket;
	st.info = nullptr;
	const sa::AssetRecord* rec = m_library.find(id);
	if (!rec || rec->missing || rec->meta.importer != "model")
	{
		st.status = "error";
		st.error  = !rec ? "アセットがありません (assets/ に .meta がない)" : rec->missing ? "元のファイルがありません: " + rec->path
		                                                                        : "モデルではありません: " + rec->path;
		log("warning", "library", "モデル " + id + ": " + st.error);
		m_render.postScene([id](GraphicsEngine& engine, ViewportRenderer* scene) {
			if (scene) scene->setModel(engine, id, nullptr);
		});
		m_host.emit("library.changed", Json::object());
		return;
	}
	st.status = "loading";
	st.error.clear();
	const uint64_t generation = m_libraryGeneration;
	m_libraryWorker.post([this, rec = *rec, cache = m_library.cacheDir(), generation, id, ticket] {
		const auto start = std::chrono::steady_clock::now();
		sa::Diagnostics diag;
		std::shared_ptr<const sa::Model> model = m_library.loadModel(rec, cache, diag);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		m_host.post([this, generation, id, ticket, model, diag, ms] { onModelLoaded(generation, id, ticket, model, diag, ms); });
	});
	m_host.emit("library.changed", Json::object());
}

void EditorApp::onModelLoaded(uint64_t generation, const std::string& id, uint64_t ticket,
                              std::shared_ptr<const sa::Model> model, const sa::Diagnostics& diag, double ms)
{
	if (generation != m_libraryGeneration)
		return;   // 前のプロジェクトの結果
	auto it = m_modelStates.find(id);
	if (it == m_modelStates.end() || it->second.ticket != ticket)
		return;   // 読み直しを頼んだあとに届いた古い結果
	ModelState& st = it->second;
	for (const auto& w : diag.warnings) log("warning", "library", w);
	if (!model)
	{
		st.status = "error";
		st.error.clear();
		for (const auto& e : diag.errors) st.error += (st.error.empty() ? "" : "\n") + e;
		log("error", "library", "モデルを読めません: " + st.error);
	}
	else
	{
		st.status = "ready";
		st.error.clear();
		st.info   = modelInfo(*model);
		st.info["loadMs"] = std::round(ms * 10) / 10;
		const sa::AssetRecord* rec = m_library.find(id);
		log("info", "library", "モデルを読みました: " + (rec ? rec->path : id) + " (" + std::to_string(st.info["triangles"].get<size_t>()) +
		                       " 三角形、" + std::to_string(static_cast<int>(ms)) + " ms)");
	}
	m_render.postScene([id, model](GraphicsEngine& engine, ViewportRenderer* scene) {
		if (scene) scene->setModel(engine, id, model.get());
	});
	m_host.emit("library.changed", Json::object());
}

void EditorApp::registerLibraryMethods()
{
	Bridge& b = m_host.bridge();

	b.registerMethod("library.list", [this](const Json&, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		r.ok(libraryJson());
	});

	b.registerMethod("library.import", [this](const Json& p, Bridge::Reply r) {
		if (!m_project || !m_library.isOpen())
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		const std::string mode = p.value("mode", std::string("copy"));
		if (mode != "copy" && mode != "link")
			return r.error(BridgeError::InvalidParams, "mode は copy か link です");
		std::filesystem::path file;
		if (p.contains("file") && p.at("file").is_string())
			file = pathFromUtf8(p.at("file").get<std::string>());
		else
		{
			const auto picked = openAssetDialog(m_host.nativeWindow(), m_library.extensions());
			if (!picked)
				return r.ok({ { "canceled", true } });
			file = *picked;
		}
		ProjectAssets::ImportResult result;
		std::string error;
		const bool ok = mode == "copy" ? m_library.importCopy(file, result, error) : m_library.importLink(file, result, error);
		if (!ok)
			return r.error(BridgeError::InvalidParams, "取り込めません: " + error);
		for (const auto& w : result.warnings)
			log("warning", "library", w);
		log("info", "library", (mode == "copy" ? "取り込みました: " : "外部のファイルを参照しました: ") + result.path +
		                       (result.copied.empty() ? "" : " (ほかに " + std::to_string(result.copied.size()) + " ファイルをコピー)"));
		m_host.emit("library.changed", Json::object());
		r.ok({ { "id", result.id }, { "path", result.path }, { "copied", result.copied }, { "warnings", result.warnings } });
	});

	b.registerMethod("library.refresh", [this](const Json&, Bridge::Reply r) {
		if (!m_project || !m_library.isOpen())
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		sa::Diagnostics diag;
		const auto scan = m_library.rescan(diag);
		for (const auto& w : diag.warnings) log("warning", "library", w);
		for (const auto& e : diag.errors)   log("error", "library", e);
		// 読んだことのあるモデルで、元のファイル / 設定が変わったもの (や、消えた / 戻ったもの) を読み直す
		std::vector<std::string> reload;
		for (const auto& [id, st] : m_modelStates)
		{
			const sa::AssetRecord* rec = m_library.find(id);
			const bool stale = !rec || rec->missing ? st.status != "error"
			                                        : st.status == "error" || !m_library.database().upToDate(*rec, m_library.cacheDir());
			if (stale && st.status != "loading")
				reload.push_back(id);
		}
		for (const auto& id : reload)
			requestModel(id);
		m_host.emit("library.changed", Json::object());
		r.ok({ { "created", scan.created }, { "reIded", scan.reIded }, { "reloaded", reload } });
	});

	b.registerMethod("library.load", [this](const Json& p, Bridge::Reply r) {
		if (!m_project || !m_library.isOpen())
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		const std::string id = p.value("id", std::string());
		if (!m_library.find(id))
			return r.error(BridgeError::InvalidParams, "アセットがありません: " + id);
		auto it = m_modelStates.find(id);
		if (it == m_modelStates.end() || it->second.status == "error")
			requestModel(id);
		r.ok({ { "id", id }, { "status", m_modelStates[id].status } });
	});

	b.registerMethod("library.reimport", [this](const Json& p, Bridge::Reply r) {
		if (!m_project || !m_library.isOpen())
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		const std::string id = p.value("id", std::string());
		const sa::AssetRecord* rec = m_library.find(id);
		if (!rec)
			return r.error(BridgeError::InvalidParams, "アセットがありません: " + id);
		std::error_code ec;
		std::filesystem::remove(sa::AssetDatabase::artifactPath(m_library.cacheDir(), rec->id), ec);
		if (rec->meta.importer == "model")
			requestModel(id);
		m_host.emit("library.changed", Json::object());
		r.ok({ { "id", id } });
	});
}
} // namespace studio
