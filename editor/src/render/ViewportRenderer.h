#pragma once
// ============================================================
//  editor/src/render/ViewportRenderer.h
//
//  ビューポートをレンダーグラフで描く (M3d)。描画の流れはすべて、開いたプロジェクトのアセットの JSON にある:
//    Assets/graphs/viewport.json   GBuffer → Lighting → Grid → Composite (プロジェクトファイルの viewport.graph)
//    Assets/pipelines/*.json       パイプライン (シェーダー、バインディングレイアウト、ステート)
//    Assets/shaders/*.hlsl         シェーダー
//  プロジェクトを開くたびに init し直す (M4a)。
//  このクラスは、シーン (立方体と地面のグリッド) とフレームの定数を IDrawListProvider としてグラフに渡し、
//  アセットの変更 (ファイルの保存、インスペクターの編集) をその場で反映する。
//
//  描画スレッドだけが使う。
// ============================================================

#include "OrbitCamera.h"

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

	// rt (バックバッファ) に描いて submit する
	void render(GraphicsEngine& engine, IRenderTarget* rt, uint32_t width, uint32_t height, const OrbitCamera& camera);

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
		BufferHandle cb;   // ワールド行列と色 (common.hlsli の Object)
	};

	std::filesystem::path                     m_root;
	std::string                               m_graphFile;
	std::unique_ptr<gfx::rg::PipelineLibrary> m_pipelines;
	std::unique_ptr<gfx::rg::Executor>        m_executor;
	std::unique_ptr<gfx::rg::GraphAsset>      m_graph;
	gfx::rg::CompileOptions                   m_options;

	BufferHandle        m_vb, m_ib, m_frameCB;
	std::vector<Object> m_objects;
	size_t              m_reportedPipelineErrors = 0;
	size_t              m_reportedExecutorErrors = 0;
};
} // namespace studio
