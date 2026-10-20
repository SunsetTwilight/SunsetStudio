// ============================================================
//  editor/src/render/UnassignedOverlay.cpp
// ============================================================
#include "UnassignedOverlay.h"

namespace studio
{
namespace
{
const char* k_OverlayVS = R"hlsl(
cbuffer Overlay : register(b0)
{
	float4x4 gMvp;     // 列優先 (C++ の Mat4)
	float4x4 gWorld;
};
struct VSOut
{
	float4 pos    : SV_Position;
	float3 normal : NORMAL;
};
VSOut main(float3 pos : POSITION, float3 normal : NORMAL)
{
	VSOut o;
	o.pos    = mul(gMvp, float4(pos, 1));
	o.normal = mul((float3x3)gWorld, normal);
	return o;
}
)hlsl";

// 光を受けないピンク。形が分かるよう、法線の向きで少しだけ明暗をつける
const char* k_OverlayPS = R"hlsl(
float4 main(float4 pos : SV_Position, float3 normal : NORMAL) : SV_Target
{
	const float3 n = normalize(normal);
	const float  k = 0.75 + 0.25 * abs(dot(n, normalize(float3(0.4, 0.8, 0.45))));
	return float4(float3(1.0, 0.0, 1.0) * k, 1);
}
)hlsl";

struct OverlayConstants
{
	Mat4 mvp;
	Mat4 world;
};
static_assert(sizeof(OverlayConstants) == 128, "ルート定数は 32 個");
} // namespace

bool UnassignedOverlay::init(GraphicsEngine& engine, std::string& error)
{
	ShaderDesc vs; vs.stage = ShaderStage::Vertex;   vs.source = k_OverlayVS; vs.sourceName = "UnassignedOverlay.vs";
	ShaderDesc ps; ps.stage = ShaderStage::Fragment; ps.source = k_OverlayPS; ps.sourceName = "UnassignedOverlay.ps";
	m_vs = engine.compileShader(vs);
	m_ps = engine.compileShader(ps);
	GfxBindingLayoutDesc layout;
	layout.addConstants(0, 32, 0, GfxShaderVisibility::Vertex);
	layout.flags     = GfxRootSignatureFlags::AllowInputAssemblerInputLayout;
	layout.debugName = "UnassignedOverlay";
	m_layout = engine.createBindingLayout(layout);

	GfxGraphicsPipelineDesc pd;
	pd.bindingLayout                    = m_layout;
	pd.vs                               = m_vs;
	pd.ps                               = m_ps;
	pd.rasterizerState.cullMode         = GfxCullMode::None;   // モデルの表の向きは決まっていないので両面
	pd.depthStencilState.depthEnable    = true;
	pd.depthStencilState.depthWriteMask = GfxDepthWriteMask::All;
	pd.depthStencilState.depthFunc      = GfxCompareFunc::Less;
	pd.inputLayout.add("POSITION", 0, GfxFormat::R32G32B32_Float, 0, 0);
	pd.inputLayout.add("NORMAL", 0, GfxFormat::R32G32B32_Float, 0, 12);
	pd.primitiveTopologyType            = GfxPrimitiveTopologyType::Triangle;
	pd.primitiveTopology                = GfxPrimitiveTopology::TriangleList;
	pd.numRenderTargets                 = 1;
	pd.rtvFormats[0]                    = GfxFormat::B8G8R8A8_Unorm;   // ビューポート / オフスクリーンのバックバッファ
	pd.dsvFormat                        = GfxFormat::D32_Float;
	pd.debugName                        = "UnassignedOverlay";
	m_pipe = engine.createPipeline(pd);
	if (!m_vs.valid() || !m_ps.valid() || !m_layout.valid() || !m_pipe.valid())
	{
		error = "どこにも描かれない物体の印 (ピンク) のパイプラインを作れませんでした";
		return false;
	}
	return true;
}

void UnassignedOverlay::shutdown(GraphicsEngine& engine)
{
	engine.destroyDepthStencil(m_depth);
	m_depth = nullptr;
	m_depthW = m_depthH = 0;
	engine.destroyPipeline(m_pipe);
	engine.destroyBindingLayout(m_layout);
	engine.destroyShader(m_vs);
	engine.destroyShader(m_ps);
	m_pipe = {};
	m_layout = {};
	m_vs = m_ps = {};
}

void UnassignedOverlay::record(GraphicsEngine& engine, ICommandBuffer* cb, IRenderTarget* rt, uint32_t w, uint32_t h,
                               const Mat4& viewProj, const std::vector<Mesh>& meshes)
{
	if (meshes.empty() || !m_pipe.valid() || !cb || !rt || w == 0 || h == 0)
		return;
	// 深度のバッファはビューポートの大きさに合わせて作り直す (エンジンが GPU の使い終わりまで解放を遅らせる)
	if (!m_depth || m_depthW != w || m_depthH != h)
	{
		engine.destroyDepthStencil(m_depth);
		DepthStencilDesc d;
		d.width     = w;
		d.height    = h;
		d.debugName = "UnassignedOverlay.Depth";
		m_depth  = engine.createDepthStencil(d);
		m_depthW = w;
		m_depthH = h;
	}
	if (!m_depth)
		return;
	cb->beginEvent("UnassignedOverlay");
	cb->setRenderTargets(1, &rt, m_depth, false, false);
	cb->clearDepthStencil(m_depth, GfxClearFlags::Depth, 1.0f, 0);
	GfxViewport vp;
	vp.width  = static_cast<float>(w);
	vp.height = static_cast<float>(h);
	cb->setViewports(1, &vp);
	GfxRect sc;
	sc.right  = static_cast<int32_t>(w);
	sc.bottom = static_cast<int32_t>(h);
	cb->setScissors(1, &sc);
	cb->bindPipeline(m_pipe);
	uint64_t lastVb = 0, lastIb = 0;
	for (const Mesh& m : meshes)
	{
		if (m.vb.id != lastVb)
		{
			const GfxVertexBufferBinding vb{ m.vb, 0, 0, 48 };
			cb->setVertexBuffers(0, 1, &vb);
			lastVb = m.vb.id;
		}
		if (m.ib.id != lastIb)
		{
			GfxIndexBufferBinding ib;
			ib.buffer = m.ib;
			ib.format = m.indexFormat;
			cb->setIndexBuffer(ib);
			lastIb = m.ib.id;
		}
		OverlayConstants c;
		c.mvp   = viewProj * m.world;
		c.world = m.world;
		cb->setRootConstants(0, 32, &c);
		cb->drawIndexedInstanced(m.indexCount, 1, m.indexOffset, 0, 0);
	}
	cb->endEvent();
}
} // namespace studio
