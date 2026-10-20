#pragma once
// ============================================================
//  editor/src/render/SelectionOutline.h
//
//  選んだ物体の印 (M5-2): 物体の箱 (-0.5〜0.5 をワールドの行列で変換したもの) の 12 本の辺を線で描く。
//  グラフの記録のあとに、同じコマンドバッファでバックバッファに重ねる (深度は見ない = 隠れていても見える)。
//  頂点バッファはなく、頂点シェーダーが SV_VertexID から辺の端を作る。
//
//  描画スレッドだけが使う。
// ============================================================

#include "OrbitCamera.h"

#include "gfx/core/GraphicsEngine.h"

#include <string>

namespace studio
{
class SelectionOutline
{
public:
	bool init(GraphicsEngine& engine, std::string& error);
	void shutdown(GraphicsEngine& engine);

	// rt (w x h) に、viewProj × world の箱の辺を描く。color が nullptr なら橙 (選択の色)
	void record(ICommandBuffer* cb, IRenderTarget* rt, uint32_t w, uint32_t h, const Mat4& viewProj, const Mat4& world,
	            const float* color = nullptr);

private:
	ShaderHandle        m_vs, m_ps;
	BindingLayoutHandle m_layout;
	PipelineHandle      m_pipe;
};
} // namespace studio
