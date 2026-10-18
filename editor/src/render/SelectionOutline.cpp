// ============================================================
//  editor/src/render/SelectionOutline.cpp
// ============================================================
#include "SelectionOutline.h"

namespace studio
{
namespace
{
// 箱の角 i = (x, y, z) のビット。1 ビットだけ違う角どうしが辺 (12 本 = 24 頂点)
const char* k_OutlineVS = R"hlsl(
cbuffer Outline : register(b0)
{
	float4x4 gMvp;     // 列優先 (C++ の Mat4)
	float4   gColor;
};
static const uint kEdges[24] = { 0,1, 2,3, 4,5, 6,7, 0,2, 1,3, 4,6, 5,7, 0,4, 1,5, 2,6, 3,7 };
float4 main(uint id : SV_VertexID) : SV_Position
{
	const uint c = kEdges[id];
	const float3 p = float3((c & 1) ? 0.5 : -0.5, (c & 2) ? 0.5 : -0.5, (c & 4) ? 0.5 : -0.5);
	return mul(gMvp, float4(p, 1));
}
)hlsl";

const char* k_OutlinePS = R"hlsl(
cbuffer Outline : register(b0)
{
	float4x4 gMvp;
	float4   gColor;
};
float4 main() : SV_Target
{
	return gColor;
}
)hlsl";

struct OutlineConstants
{
	Mat4  mvp;
	float color[4];
};
static_assert(sizeof(OutlineConstants) == 80, "ルート定数は 20 個");
} // namespace

bool SelectionOutline::init(GraphicsEngine& engine, std::string& error)
{
	ShaderDesc vs; vs.stage = ShaderStage::Vertex;   vs.source = k_OutlineVS; vs.sourceName = "SelectionOutline.vs";
	ShaderDesc ps; ps.stage = ShaderStage::Fragment; ps.source = k_OutlinePS; ps.sourceName = "SelectionOutline.ps";
	m_vs = engine.compileShader(vs);
	m_ps = engine.compileShader(ps);
	GfxBindingLayoutDesc layout;
	layout.addConstants(0, 20, 0, GfxShaderVisibility::All);
	layout.debugName = "SelectionOutline";
	m_layout = engine.createBindingLayout(layout);

	GfxGraphicsPipelineDesc pd;
	pd.bindingLayout                    = m_layout;
	pd.vs                               = m_vs;
	pd.ps                               = m_ps;
	pd.rasterizerState.cullMode         = GfxCullMode::None;
	pd.depthStencilState.depthEnable    = false;
	pd.depthStencilState.depthWriteMask = GfxDepthWriteMask::Zero;
	pd.primitiveTopologyType            = GfxPrimitiveTopologyType::Line;
	pd.primitiveTopology                = GfxPrimitiveTopology::LineList;
	pd.numRenderTargets                 = 1;
	pd.rtvFormats[0]                    = GfxFormat::B8G8R8A8_Unorm;   // ビューポート / オフスクリーンのバックバッファ
	pd.debugName                        = "SelectionOutline";
	m_pipe = engine.createPipeline(pd);
	if (!m_vs.valid() || !m_ps.valid() || !m_layout.valid() || !m_pipe.valid())
	{
		error = "選択の印のパイプラインを作れませんでした";
		return false;
	}
	return true;
}

void SelectionOutline::shutdown(GraphicsEngine& engine)
{
	engine.destroyPipeline(m_pipe);
	engine.destroyBindingLayout(m_layout);
	engine.destroyShader(m_vs);
	engine.destroyShader(m_ps);
	m_pipe = {};
	m_layout = {};
	m_vs = m_ps = {};
}

void SelectionOutline::record(ICommandBuffer* cb, IRenderTarget* rt, uint32_t w, uint32_t h, const Mat4& viewProj, const Mat4& world)
{
	if (!m_pipe.valid() || !cb || !rt || w == 0 || h == 0)
		return;
	OutlineConstants c;
	c.mvp = viewProj * world;
	c.color[0] = 1.0f; c.color[1] = 0.62f; c.color[2] = 0.15f; c.color[3] = 1.0f;   // 橙 (UI のアクセントの色)
	cb->beginEvent("SelectionOutline");
	cb->setRenderTargets(1, &rt, nullptr, false, false);
	GfxViewport vp;
	vp.width  = static_cast<float>(w);
	vp.height = static_cast<float>(h);
	cb->setViewports(1, &vp);
	GfxRect sc;
	sc.right  = static_cast<int32_t>(w);
	sc.bottom = static_cast<int32_t>(h);
	cb->setScissors(1, &sc);
	cb->bindPipeline(m_pipe);
	cb->setRootConstants(0, 20, &c);
	cb->drawInstanced(24);
	cb->endEvent();
}
} // namespace studio
