#pragma once
// ============================================================
//  editor/src/render/ViewportRenderer.h
//
//  ビューポートをレンダーグラフで描く (M3d)。描画の流れはすべて、開いたプロジェクトのアセットの JSON にある:
//    Assets/graphs/viewport.json   GBuffer → Lighting → Grid → Composite (プロジェクトファイルの viewport.graph)
//    Assets/pipelines/*.json       パイプライン (シェーダー、バインディングレイアウト、ステート)
//    Assets/shaders/*.hlsl         シェーダー
//  プロジェクトを開くたびに init し直す (M4a)。
//  このクラスは、シーン (シーンのファイルの物体と地面のグリッド) とフレームの定数を IDrawListProvider としてグラフに渡し、
//  アセットの変更 (ファイルの保存、インスペクターの編集) をその場で反映する。
//
//  描画スレッドだけが使う。
// ============================================================

#include "ModelCache.h"
#include "OrbitCamera.h"
#include "ResourceViewer.h"
#include "SelectionOutline.h"
#include "services/Scene.h"

#include "gfx/core/GraphicsEngine.h"
#include "gfx/core/ShaderLibrary.h"
#include "gfx/rendergraph/Executor.h"
#include "gfx/rendergraph/GraphAsset.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace studio
{
class ViewportRenderer final : public gfx::rg::IDrawListProvider
{
public:
	// 失敗したら false (error に理由)。graphFile は assetRoot からの相対パス
	bool init(GraphicsEngine& engine, ShaderLibrary& shaders, const std::filesystem::path& assetRoot,
	          const std::string& graphFile, std::string& error);
	void shutdown(GraphicsEngine& engine);

	// アセットの変更を反映する (毎フレーム)。結果はログに出す内容
	struct PollResult
	{
		std::vector<std::string> reloadedPipelines;
		std::vector<std::string> failedPipelines;
		bool                     graphReloaded   = false;   // ファイルを読み直した
		bool                     graphRecompiled = false;   // パイプラインが変わったのでコンパイルし直した
		bool                     graphFailed     = false;   // 読めない / コンパイルできないので前のまま
		std::vector<std::string> graphMessages;
	};
	PollResult pollAssets();

	// rt (バックバッファ) に描いて submit する。リソースビューアーが有効なら、グラフの結果の代わりにリソースを表示する
	// (cursorX < 0 ならカーソルはビューポートの外)
	void render(GraphicsEngine& engine, IRenderTarget* rt, uint32_t width, uint32_t height, const OrbitCamera& camera,
	            int cursorX = -1, int cursorY = -1);

	// render のあとで、プレビューのウィンドウ (rt) に settings のリソースを描いて submit する。表示できなければ status に理由
	void drawPreview(GraphicsEngine& engine, IRenderTarget* rt, uint32_t width, uint32_t height,
	                 const ResourceViewer::Settings& settings, ResourceViewer::ViewCache& cache, const OrbitCamera& camera,
	                 std::string& status);

	// シーンの物体を置き換える (M5-2。drawList の layer が同じ、見えている物体を描く。
	// asset:<ID> のモデルは setModel で届いてから描く。届くまでは何も描かない)
	void setScene(GraphicsEngine& engine, const SceneDesc& scene);
	// 取り込んだモデルを入れる / 入れ替える (M5-3)。model が nullptr なら外す
	void setModel(GraphicsEngine& engine, const std::string& id, const sunset::asset::Model* model);
	// 選んでいる物体 (空なら選んでいない)。render で辺の印を描く
	void setSelection(const std::string& id) { m_selection = id; }
	// ビューポートのピクセル (x, y) の下の物体の id (なければ空)
	std::string pick(int x, int y, uint32_t width, uint32_t height, const OrbitCamera& camera) const;

	ResourceViewer& viewer() { return m_viewer; }
	bool            viewerOk() const { return m_viewerOk; }

	gfx::rg::GraphAsset&            graph()     { return *m_graph; }
	gfx::rg::PipelineLibrary&       pipelines() { return *m_pipelines; }
	gfx::rg::Executor&              executor()  { return *m_executor; }
	const gfx::rg::CompileOptions&  compileOptions() const { return m_options; }
	const std::filesystem::path&    assetRoot() const { return m_root; }
	const std::string&              graphFile() const { return m_graphFile; }

	// プロジェクトのパイプラインのアセット (pipelines/*.json。区切りは "/")
	std::vector<std::string> pipelineFiles() const;

	// エラーの記録の新しい分 (ログに出す)
	std::vector<std::string> takeNewErrors();

	// ---- IDrawListProvider ----
	void         draw(const gfx::rg::DrawListQuery& query, const gfx::rg::DrawContext& ctx) override;
	BufferHandle constants(const std::string& source, const gfx::rg::DrawContext& ctx) override;

private:
	struct Object
	{
		BufferHandle cb;   // ワールド行列と色 (common.hlsli の Object)。builtin:cube
		std::vector<BufferHandle> drawCBs;   // asset:<ID> のモデル: ModelCache::Draw ごと
		std::string  id;
		std::string  layer;
		std::string  modelId;          // asset:<ID> の ID (空なら builtin:cube)
		bool         visible = true;
		bool         cube    = true;   // builtin:cube
		Mat4         world;
		Mat4         box;              // 選択の判定と印の箱 (単位の箱をワールドへ)
	};
	void updateObjects(GraphicsEngine& engine);   // m_sceneDesc と m_models から m_objects を作り直す

	std::filesystem::path                     m_root;
	std::string                               m_graphFile;
	std::unique_ptr<gfx::rg::PipelineLibrary> m_pipelines;
	std::unique_ptr<gfx::rg::Executor>        m_executor;
	std::unique_ptr<gfx::rg::GraphAsset>      m_graph;
	gfx::rg::CompileOptions                   m_options;

	ResourceViewer      m_viewer;
	bool                m_viewerOk = false;
	SelectionOutline    m_outline;
	bool                m_outlineOk = false;
	SceneDesc           m_sceneDesc;     // 選択の判定に使う
	std::string         m_selection;
	std::string         m_viewerError;
	BufferHandle        m_vb, m_ib, m_frameCB;
	std::vector<Object> m_objects;
	ModelCache          m_models;
	size_t              m_reportedPipelineErrors = 0;
	size_t              m_reportedExecutorErrors = 0;
};
} // namespace studio
