// ============================================================
//  editor/src/app/EditorSave.cpp
//
//  保存していない変更と、まとめての保存 (2026-10-21)。
//    project.unsaved   保存していないもの: { graph, pipelines: [...], scene, any }
//    project.saveAll   保存していないものをすべてファイルに書く (グラフ、パイプライン、シーン)
//    app.closeAnswer   閉じるときの確認の答え { action: "save" | "discard" | "cancel" }
//  イベント:
//    unsaved.changed   project.unsaved と同じ形 (編集、元に戻す / やり直す、保存、ファイルの読み直しのあと)
//    app.closeRequested  保存していない変更があるのに、ウィンドウを閉じようとした (UI が確認を出す)
//
//  グラフとパイプラインは、エディターの中の内容とファイルの内容を同じ形 (JSON の文字列) に直して比べる。
//  なので、元に戻すで保存した状態に戻ったときや、ファイルを外で書いて読み直したときも「保存済み」になる。
//  シーンは EditorScene.cpp の m_sceneDirty。
// ============================================================
#include "EditorApp.h"

#include "gfx/rendergraph/GraphJson.h"

namespace studio
{
namespace
{
// エディターの中のパイプラインのアセットが、ファイルと違うか (読めないファイルは違うとみなさない)
bool pipelineDiffers(ViewportRenderer& scene, const std::string& id)
{
	const gfx::rg::PipelineAsset* current = scene.pipelines().asset(id);
	if (!current)
		return false;
	std::string text;
	if (!gfx::rg::readTextFile(scene.assetRoot() / gfx::rg::pathFromUtf8(id), text))
		return true;   // ファイルが消えた
	GfxJson json;
	gfx::rg::PipelineAsset onDisk;
	if (!gfxJsonParse(text, json) || !gfx::rg::pipelineAssetFromJson(json, onDisk))
		return false;
	return gfxJsonStringify(gfx::rg::pipelineAssetToJson(*current)) != gfxJsonStringify(gfx::rg::pipelineAssetToJson(onDisk));
}

bool graphDiffers(ViewportRenderer& scene)
{
	if (!scene.graph().valid())
		return false;
	std::string text;
	if (!gfx::rg::readTextFile(scene.graph().file(), text))
		return true;
	gfx::rg::GraphDesc onDisk;
	if (!gfx::rg::graphFromString(text, onDisk))
		return false;
	return gfx::rg::graphToString(scene.graph().desc()) != gfx::rg::graphToString(onDisk);
}
} // namespace

Json EditorApp::unsavedJson() const
{
	const bool any = m_unsavedGraph || !m_unsavedPipelines.empty() || m_sceneDirty;
	return { { "graph", m_unsavedGraph }, { "pipelines", m_unsavedPipelines }, { "scene", m_sceneDirty }, { "any", any } };
}

void EditorApp::refreshUnsaved()
{
	if (!m_project)
	{
		m_unsavedGraph = false;
		m_unsavedPipelines.clear();
		m_host.emit("unsaved.changed", unsavedJson());
		return;
	}
	if (m_unsavedBusy)
	{
		m_unsavedAgain = true;   // 調べている途中: 終わったらもう一度
		return;
	}
	m_unsavedBusy = true;
	m_render.postScene([this](GraphicsEngine&, ViewportRenderer* scene) {
		bool graph = false;
		std::vector<std::string> pipelines;
		if (scene)
		{
			graph = graphDiffers(*scene);
			for (const auto& id : scene->pipelineFiles())
				if (pipelineDiffers(*scene, id))
					pipelines.push_back(id);
		}
		m_host.post([this, graph, pipelines] {
			m_unsavedBusy  = false;
			m_unsavedGraph = graph;
			m_unsavedPipelines = pipelines;
			m_host.emit("unsaved.changed", unsavedJson());
			if (m_unsavedAgain)
			{
				m_unsavedAgain = false;
				refreshUnsaved();
			}
		});
	});
}

void EditorApp::saveAll(std::function<void(bool ok, const Json& result, const std::string& error)> done)
{
	if (!m_project)
		return done(false, {}, "プロジェクトを開いていません");
	m_render.postScene([this, done](GraphicsEngine&, ViewportRenderer* scene) {
		Json saved = Json::array();
		std::string error;
		if (scene)
		{
			if (graphDiffers(*scene))
			{
				if (scene->graph().save())
					saved.push_back(scene->graphFile());
				else
					error += "グラフを保存できません: " + scene->graphFile() + "\n";
			}
			for (const auto& id : scene->pipelineFiles())
				if (pipelineDiffers(*scene, id))
				{
					if (scene->pipelines().save(id))
						saved.push_back(id);
					else
						error += "パイプラインを保存できません: " + id + "\n";
				}
		}
		m_host.post([this, done, saved, error]() mutable {
			auto finish = [this, done, saved](std::string err) {
				for (const auto& s : saved)
					log("info", "project", "保存しました: " + s.get<std::string>());
				refreshUnsaved();
				if (!err.empty())
					return done(false, { { "saved", saved } }, err);
				done(true, { { "saved", saved } }, {});
			};
			if (!m_sceneDirty)
				return finish(error);
			m_host.bridge().callLocal("scene.save", Json::object(), [this, finish, error, saved](int status, const Json& r) mutable {
				m_host.post([this, finish, error, status, r, saved]() mutable {
					std::string err = error;
					if (status != 0)
						err += "シーンを保存できません: " + r.value("message", r.dump()) + "\n";
					finish(err);
				});
			});
		});
	});
}

void EditorApp::registerSaveMethods()
{
	Bridge& b = m_host.bridge();

	b.registerMethod("project.unsaved", [this](const Json&, Bridge::Reply r) { r.ok(unsavedJson()); });

	b.registerMethod("project.saveAll", [this](const Json&, Bridge::Reply r) {
		saveAll([r](bool ok, const Json& result, const std::string& error) {
			if (!ok)
				return r.error(BridgeError::Internal, error);
			r.ok(result);
		});
	});

	// 閉じるときの確認の答え。save なら保存してから閉じる (保存に失敗したら閉じない)、discard ならそのまま閉じる
	b.registerMethod("app.closeAnswer", [this](const Json& p, Bridge::Reply r) {
		const std::string action = p.value("action", "cancel");
		if (action == "cancel")
		{
			m_host.cancelClose();
			return r.ok({ { "closing", false } });
		}
		if (action == "discard")
		{
			m_closeConfirmed = true;
			m_host.cancelClose();
			m_host.closeWindow();
			return r.ok({ { "closing", true } });
		}
		saveAll([this, r](bool ok, const Json&, const std::string& error) {
			if (!ok)
			{
				m_host.cancelClose();
				return r.error(BridgeError::Internal, error);
			}
			m_closeConfirmed = true;
			m_host.cancelClose();
			m_host.closeWindow();
			r.ok({ { "closing", true } });
		});
	});
}
} // namespace studio
