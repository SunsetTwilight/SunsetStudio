#pragma once
// ============================================================
//  editor/src/render/UnassignedOverlay.h
//
//  どの描画ノードにも入らないメッシュの印: グラフの記録のあとに、バックバッファへピンク (光を受けない。面の向きで
//  少しだけ明暗をつける) で重ねて描く。深度は自前のバッファ (グラフの深度は見ない) で、印どうしの前後だけを決める。
//
//  描画スレッドだけが使う。
// ============================================================

#include "OrbitCamera.h"

#include "gfx/core/GraphicsEngine.h"

#include <string>
#include <vector>

namespace studio
{
class UnassignedOverlay
{
public:
	struct Mesh
	{
		BufferHandle vb, ib;   // 頂点は MeshVertex (48 バイト。位置と法線を使う)
		GfxFormat    indexFormat = GfxFormat::R32_Uint;
		uint32_t     indexCount  = 0;
		uint32_t     indexOffset = 0;
		Mat4         world;
	};

	bool init(GraphicsEngine& engine, std::string& error);
	void shutdown(GraphicsEngine& engine);

	// rt (w x h) に meshes を描く。meshes が空なら何もしない
	void record(GraphicsEngine& engine, ICommandBuffer* cb, IRenderTarget* rt, uint32_t w, uint32_t h, const Mat4& viewProj,
	            const std::vector<Mesh>& meshes);

private:
	ShaderHandle        m_vs, m_ps;
	BindingLayoutHandle m_layout;
	PipelineHandle      m_pipe;
	IDepthStencil*      m_depth = nullptr;
	uint32_t            m_depthW = 0, m_depthH = 0;
};
} // namespace studio
