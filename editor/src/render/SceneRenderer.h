#pragma once
// ============================================================
//  editor/src/render/SceneRenderer.h
//
//  ビューポートに描くシーン (M2b)。並べた立方体と地面のグリッドを、1 パスのフォワードで描く。
//  シェーダーは editor/shaders/*.hlsl を ShaderLibrary で読むので、編集すればその場で反映される。
//  ディファードの合成はレンダーグラフ (M3) の上で作り直す。
//
//  描画スレッドだけが使う。
// ============================================================

#include "OrbitCamera.h"

#include "gfx/core/GraphicsEngine.h"
#include "gfx/core/ShaderLibrary.h"

#include <string>
#include <vector>

namespace studio
{
class SceneRenderer
{
public:
	// 失敗したら false (error に理由)
	bool init(GraphicsEngine& engine, ShaderLibrary& shaders, std::string& error);
	void shutdown(GraphicsEngine& engine);

	// rt (バックバッファ) に描いて submit する。深度バッファは rt の大きさに合わせて作り直す
	void render(GraphicsEngine& engine, IRenderTarget* rt, uint32_t width, uint32_t height, const OrbitCamera& camera);

	// インスペクターで編集できるパイプライン
	struct PipelineEntry
	{
		std::string    id;      // "scene" / "grid"
		std::string    label;   // 表示名
		PipelineHandle handle;
	};
	const std::vector<PipelineEntry>& pipelines() const { return m_pipelineList; }
	const PipelineEntry* findPipeline(const std::string& id) const
	{
		for (const auto& p : m_pipelineList)
			if (p.id == id)
				return &p;
		return nullptr;
	}

private:
	struct ObjectConstants
	{
		Mat4  world;
		float color[4];
	};
	static_assert(sizeof(ObjectConstants) == 20 * 4, "ルート定数は 20 個");

	void ensureDepth(GraphicsEngine& engine, uint32_t width, uint32_t height);

	BindingLayoutHandle m_layout;
	PipelineHandle      m_scenePipe;
	PipelineHandle      m_gridPipe;
	BufferHandle        m_vb, m_ib, m_frameCB;
	IDepthStencil*      m_depth = nullptr;
	uint32_t            m_depthW = 0, m_depthH = 0;
	std::vector<ObjectConstants> m_objects;
	std::vector<PipelineEntry>   m_pipelineList;
};
} // namespace studio
