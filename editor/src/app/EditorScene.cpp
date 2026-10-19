// ============================================================
//  editor/src/app/EditorScene.cpp
//
//  シーンのファイル (M5-2): 何を描くか。データは UI スレッドが持ち (m_sceneData)、描画スレッドにはコピーを渡す。
//    scene.get      シーンの JSON、ファイル、版、保存していない変更があるか
//    scene.set      シーン全体の JSON を受け取り、検証して描画に反映する (元に戻す / やり直すの対象。保存はしない)
//    scene.save     ファイルに書く
//    scene.select   選んでいる物体 (ビューポートの辺の印)
//    scene.pickAt   ビューポートのピクセルの下の物体 (テスト用。クリックの選択と同じ判定)
//  イベント: scene.changed (版と保存の状態が変わった) / scene.picked (ビューポートのクリックで選んだ物体)
//  ファイルがなければ、既定のシーン (M4 までの固定の立方体と同じ並び) を書く。
// ============================================================
#include "EditorApp.h"

#include "gfx/rendergraph/GraphAsset.h"

namespace studio
{
Json EditorApp::sceneInfoJson() const
{
	Json j = {
		{ "file", m_sceneFile }, { "revision", m_sceneRevision }, { "dirty", m_sceneDirty },
		{ "scene", sceneToJson(m_sceneData) },
	};
	if (!m_sceneError.empty())
		j["error"] = m_sceneError;
	return j;
}

void EditorApp::emitSceneChanged()
{
	m_host.emit("scene.changed", { { "revision", m_sceneRevision }, { "dirty", m_sceneDirty } });
}

void EditorApp::pushSceneToRenderer()
{
	requestSceneModels();
	SceneDesc copy = m_sceneData;
	const std::string selection = m_sceneSelection;
	m_render.postScene([copy = std::move(copy), selection](GraphicsEngine& engine, ViewportRenderer* scene) {
		if (!scene)
			return;
		scene->setScene(engine, copy);
		scene->setSelection(selection);
	});
}

void EditorApp::loadProjectScene()
{
	openLibrary();
	m_sceneData = {};
	m_sceneError.clear();
	m_sceneDirty = false;
	m_sceneSelection.clear();
	++m_sceneRevision;
	if (!m_project)
	{
		m_sceneFile.clear();
		return;
	}
	m_sceneFile = m_project->desc.viewportScene;
	const std::filesystem::path file = m_project->assetRoot() / gfx::rg::pathFromUtf8(m_sceneFile);
	std::error_code ec;
	if (!std::filesystem::exists(file, ec))
	{
		// シーンのないプロジェクト (M5-2 より前に作ったもの): 今までの固定の立方体と同じ並びのシーンを書く
		m_sceneData = defaultScene();
		std::string error;
		if (gfx::rg::writeTextFileAtomic(file, sceneToJson(m_sceneData).dump(2) + "\n", &error))
			log("info", "scene", "シーンがないので、既定のシーンを作りました: " + m_sceneFile);
		else
			log("error", "scene", "既定のシーンを書けません: " + m_sceneFile + " " + error);
	}
	else
	{
		std::string text;
		SceneDiagnostics diag;
		Json json;
		bool ok = gfx::rg::readTextFile(file, text);
		if (ok)
		{
			try { json = Json::parse(text); }
			catch (const std::exception& e) { diag.errors.push_back(std::string("JSON: ") + e.what()); ok = false; }
		}
		else
			diag.errors.push_back("読めません");
		if (ok && sceneFromJson(json, m_sceneData, diag))
		{
			for (const auto& w : diag.warnings)
				log("warning", "scene", m_sceneFile + ": " + w);
		}
		else
		{
			// 読めないシーンは空にして、上書きしない (保存を断る)
			m_sceneData  = {};
			m_sceneError = m_sceneFile + " を読めません";
			for (const auto& e : diag.errors)
				m_sceneError += "\n" + e;
			log("error", "scene", m_sceneError);
		}
	}
	pushSceneToRenderer();
	emitSceneChanged();
}

// 元に戻す / やり直すから (UI スレッド)。描画スレッドにはすでに渡してある
void EditorApp::applySceneFromUndo(const SceneDesc& scene)
{
	m_sceneData = scene;
	m_sceneDirty = true;
	requestSceneModels();
	++m_sceneRevision;
	if (!m_sceneSelection.empty() && !m_sceneData.find(m_sceneSelection))
		m_sceneSelection.clear();
	emitSceneChanged();
}

void EditorApp::registerSceneMethods()
{
	Bridge& b = m_host.bridge();

	b.registerMethod("scene.get", [this](const Json&, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		r.ok(sceneInfoJson());
	});

	// シーン全体を置き換える。label は履歴の名前
	b.registerMethod("scene.set", [this](const Json& p, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		if (!m_sceneError.empty())
			return r.error(BridgeError::Unavailable, "シーンのファイルを読めなかったので、編集できません (ファイルを直して開き直してください)");
		SceneDesc next;
		SceneDiagnostics diag;
		if (!p.contains("scene") || !sceneFromJson(p.at("scene"), next, diag))
		{
			std::string message = "シーンが正しくありません";
			for (const auto& e : diag.errors) message += "\n" + e;
			return r.error(BridgeError::InvalidParams, message);
		}
		Json before = sceneToJson(m_sceneData);
		Json after  = sceneToJson(next);
		if (before == after)
			return r.ok({ { "revision", m_sceneRevision }, { "dirty", m_sceneDirty }, { "warnings", diag.warnings } });
		m_sceneData = std::move(next);
		m_sceneDirty = true;
		++m_sceneRevision;
		if (!m_sceneSelection.empty() && !m_sceneData.find(m_sceneSelection))
			m_sceneSelection.clear();
		pushSceneToRenderer();
		recordEdit({ p.value("label", std::string("シーンを編集")), "scene", m_sceneFile, std::move(before), std::move(after) });
		emitSceneChanged();
		r.ok({ { "revision", m_sceneRevision }, { "dirty", m_sceneDirty }, { "warnings", diag.warnings } });
	});

	b.registerMethod("scene.save", [this](const Json&, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		if (!m_sceneError.empty())
			return r.error(BridgeError::Unavailable, "シーンのファイルを読めなかったので、上書きしません");
		const std::filesystem::path file = m_project->assetRoot() / gfx::rg::pathFromUtf8(m_sceneFile);
		std::string error;
		if (!gfx::rg::writeTextFileAtomic(file, sceneToJson(m_sceneData).dump(2) + "\n", &error))
			return r.error(BridgeError::Internal, "保存できません: " + m_sceneFile + " " + error);
		m_sceneDirty = false;
		log("info", "scene", "保存しました: " + m_sceneFile);
		emitSceneChanged();
		r.ok({ { "revision", m_sceneRevision }, { "dirty", m_sceneDirty } });
	});

	b.registerMethod("scene.select", [this](const Json& p, Bridge::Reply r) {
		m_sceneSelection = p.value("id", std::string());
		const std::string id = m_sceneSelection;
		m_render.postScene([id](GraphicsEngine&, ViewportRenderer* scene) {
			if (scene)
				scene->setSelection(id);
		});
		r.ok({ { "id", id } });
	});

	// ビューポートの (x, y) (物理ピクセル) の下の物体。クリックの選択と同じ判定 (テスト用)
	b.registerMethod("scene.pickAt", [this](const Json& p, Bridge::Reply r) {
		const int x = p.value("x", -1), y = p.value("y", -1);
		const uint32_t w = m_viewport.width(), h = m_viewport.height();
		m_render.postScene([this, r, x, y, w, h](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
			r.ok({ { "id", scene->pick(x, y, w, h, m_render.camera()) } });
		});
	});
}
} // namespace studio
