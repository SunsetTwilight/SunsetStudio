// ============================================================
//  editor/src/render/ResourceViewer.cpp
// ============================================================
#include "ResourceViewer.h"

#include "gfx/schema/GfxSchema.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace studio
{
namespace
{
const char* k_ViewVS = R"hlsl(
float4 main(uint id : SV_VertexID) : SV_Position
{
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
)hlsl";

// 表示: 矩形の中にテクスチャを Load で並べる (拡大は最近傍)。外は暗い市松模様
const char* k_ViewPS = R"hlsl(
cbuffer View : register(b0)
{
	float4 gRect;    // 表示する矩形 (ピクセル): x, y, w, h
	float4 gRange;   // min, max, 露出の倍率, 深度を線形化するか
	float4 gDepth;   // near, far, ガンマ, 未使用
	uint4  gInfo;    // テクスチャの幅, 高さ, チャンネル (bit0..3 = RGBA), 未使用
};
Texture2D<float4> gTex : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target
{
	const float2 p = pos.xy - gRect.xy;
	if (p.x < 0 || p.y < 0 || p.x >= gRect.z || p.y >= gRect.w)
	{
		const uint2 c = uint2(pos.xy) / 16;
		return float4((((c.x + c.y) & 1) ? 0.10 : 0.07).xxx, 1);
	}
	const int2 t = min(int2(p / gRect.zw * float2(gInfo.xy)), int2(gInfo.xy) - 1);
	float4 v = gTex.Load(int3(t, 0));
	if (gRange.w > 0.5)
	{
		const float zn = gDepth.x, zf = gDepth.y;
		v = (zn * zf / (zf - v.r * (zf - zn))).xxxx;   // 視点からの距離
	}
	else
		v *= gRange.z;
	v = saturate((v - gRange.x) / max(gRange.y - gRange.x, 1e-6));

	const uint  mask  = gInfo.z;
	const uint  count = countbits(mask & 0xF);
	float3 c;
	if (count == 1)   // 1 つだけなら白黒
		c = ((mask & 1) ? v.r : (mask & 2) ? v.g : (mask & 4) ? v.b : v.a).xxx;
	else
		c = float3((mask & 1) ? v.r : 0, (mask & 2) ? v.g : 0, (mask & 4) ? v.b : 0);
	if (gDepth.z > 0.5)
		c = pow(c, 1.0 / 2.2);
	return float4(c, 1);
}
)hlsl";

// ピクセルの値: 1 テクセルを読んでバッファに書く
const char* k_ProbeCS = R"hlsl(
cbuffer Probe : register(b0) { uint4 gPos; };
Texture2D<float4>   gTex : register(t0);
RWByteAddressBuffer gOut : register(u0);
[numthreads(1, 1, 1)]
void main()
{
	gOut.Store4(0, asuint(gTex.Load(int3(gPos.xy, 0))));
}
)hlsl";

struct ViewConstants
{
	float    rect[4];
	float    range[4];
	float    depth[4];
	uint32_t info[4];
};
static_assert(sizeof(ViewConstants) == 64, "ルート定数は 16 個");

bool isDepthFormat(GfxFormat f)
{
	return f == GfxFormat::D32_Float || f == GfxFormat::D24_Unorm_S8_Uint || f == GfxFormat::D16_Unorm || f == GfxFormat::D32_Float_S8X24_Uint;
}

// Texture2D<float4> で読めない (整数の) フォーマット
bool isIntegerFormat(GfxFormat f)
{
	const char* name = gfxEnumName(gfxEnumInfo<GfxFormat>(), static_cast<uint64_t>(f));
	return name && (std::strstr(name, "Uint") || std::strstr(name, "Sint")) && !isDepthFormat(f);
}
} // namespace

bool ResourceViewer::init(GraphicsEngine& engine, std::string& error)
{
	ShaderDesc vs; vs.stage = ShaderStage::Vertex;   vs.source = k_ViewVS;  vs.sourceName = "ResourceViewer.vs";
	ShaderDesc ps; ps.stage = ShaderStage::Fragment; ps.source = k_ViewPS;  ps.sourceName = "ResourceViewer.ps";
	ShaderDesc cs; cs.stage = ShaderStage::Compute;  cs.source = k_ProbeCS; cs.sourceName = "ResourceViewer.cs";
	m_vs = engine.compileShader(vs);
	m_ps = engine.compileShader(ps);
	m_cs = engine.compileShader(cs);
	if (!m_vs.valid() || !m_ps.valid() || !m_cs.valid())
	{
		error = "リソースビューアーのシェーダーを作れませんでした";
		return false;
	}

	GfxBindingLayoutDesc view;
	view.addConstants(0, 16, 0, GfxShaderVisibility::Pixel);
	view.beginTable(GfxShaderVisibility::Pixel).addRange(GfxDescriptorRangeType::SRV, 1, 0, 0, GfxDescriptorRangeFlags::DescriptorsVolatile);
	view.debugName = "ResourceViewer";
	m_viewLayout = engine.createBindingLayout(view);

	GfxBindingLayoutDesc probe;
	probe.addConstants(0, 4);
	probe.beginTable(GfxShaderVisibility::All).addRange(GfxDescriptorRangeType::SRV, 1, 0, 0, GfxDescriptorRangeFlags::DescriptorsVolatile);
	probe.beginTable(GfxShaderVisibility::All).addRange(GfxDescriptorRangeType::UAV, 1, 0, 0, GfxDescriptorRangeFlags::DescriptorsVolatile);
	probe.debugName = "ResourceViewerProbe";
	m_probeLayout = engine.createBindingLayout(probe);

	GfxGraphicsPipelineDesc pd;
	pd.bindingLayout                    = m_viewLayout;
	pd.vs                               = m_vs;
	pd.ps                               = m_ps;
	pd.rasterizerState.cullMode         = GfxCullMode::None;
	pd.depthStencilState.depthEnable    = false;
	pd.depthStencilState.depthWriteMask = GfxDepthWriteMask::Zero;
	pd.numRenderTargets                 = 1;
	pd.rtvFormats[0]                    = GfxFormat::B8G8R8A8_Unorm;   // ビューポートのバックバッファ
	pd.debugName                        = "ResourceViewer";
	m_viewPipe = engine.createPipeline(pd);

	GfxComputePipelineDesc cd;
	cd.bindingLayout = m_probeLayout;
	cd.cs            = m_cs;
	cd.debugName     = "ResourceViewerProbe";
	m_probePipe = engine.createPipeline(cd);

	m_probeBuffer = engine.createBuffer({ 16, BufferUsage::Storage, nullptr, 0 });
	if (m_probeBuffer.valid())
	{
		GfxViewDesc u;
		u.type        = GfxViewType::UAV;
		u.resource    = gfxRef(m_probeBuffer);
		u.dimension   = GfxViewDimension::Buffer;
		u.rawBuffer   = true;
		u.numElements = 4;
		u.debugName   = "ResourceViewerProbe";
		m_probeUav = engine.createView(u);
	}
	bool ok = m_viewLayout.valid() && m_probeLayout.valid() && m_viewPipe.valid() && m_probePipe.valid() && m_probeUav.valid();
	for (auto& s : m_slots)
	{
		s.readback = engine.createBuffer({ 16, BufferUsage::Readback, nullptr, 0 });
		ok = ok && s.readback.valid();
	}
	if (!ok)
		error = "リソースビューアーのパイプライン / バッファを作れませんでした (エンジン 0.4 以降が必要です)";
	return ok;
}

void ResourceViewer::releaseView(GraphicsEngine& engine)
{
	if (m_srv.valid())
		engine.destroyView(m_srv);   // GPU が使い終わるまでエンジンが解放を遅らせる
	m_srv        = {};
	m_srvTexture = 0;
}

void ResourceViewer::shutdown(GraphicsEngine& engine)
{
	releaseView(engine);
	if (m_probeUav.valid()) engine.destroyView(m_probeUav);
	engine.destroyBuffer(m_probeBuffer);
	for (auto& s : m_slots)
	{
		engine.destroyBuffer(s.readback);
		s = {};
	}
	engine.destroyPipeline(m_viewPipe);
	engine.destroyPipeline(m_probePipe);
	engine.destroyBindingLayout(m_viewLayout);
	engine.destroyBindingLayout(m_probeLayout);
	engine.destroyShader(m_vs);
	engine.destroyShader(m_ps);
	engine.destroyShader(m_cs);
	m_probeUav = {};
	m_probeBuffer = {};
	m_viewPipe = m_probePipe = {};
	m_viewLayout = m_probeLayout = {};
	m_vs = m_ps = m_cs = {};
}

void ResourceViewer::record(GraphicsEngine& engine, ICommandBuffer* cb, const gfx::rg::Executor& executor,
                            IRenderTarget* rt, uint32_t w, uint32_t h, int cursorX, int cursorY, float zNear, float zFar)
{
	++m_frame;
	m_status.clear();
	if (!m_settings.enabled || !cb || !rt || w == 0 || h == 0)
		return;

	gfx::rg::RecordedTexture tex;
	if (m_settings.resource.empty())
		return void(m_status = "表示するリソースを選んでください");
	if (!executor.recordedTexture(m_settings.resource, tex))
		return void(m_status = "'" + m_settings.resource + "' はこのフレームで使われていないか、グラフのテクスチャではありません");
	if (tex.dimension != gfx::rg::TextureDimension::Tex2D || tex.layers != 1)
		return void(m_status = "配列 / キューブ / 3D のテクスチャはまだ表示できません");
	if (tex.samples > 1)
		return void(m_status = "MSAA のテクスチャはまだ表示できません (解決したテクスチャを選んでください)");
	if (isIntegerFormat(tex.format))
		return void(m_status = "整数のフォーマットはまだ表示できません");
	const uint32_t mip = std::min(m_settings.mip, tex.mips - 1);

	// 表示する mip の SRV (テクスチャか mip が変わったら作り直す)
	if (!m_srv.valid() || m_srvTexture != tex.handle.id || m_srvMip != mip)
	{
		releaseView(engine);
		GfxViewDesc d;
		d.type            = GfxViewType::SRV;
		d.resource        = gfxRef(tex.handle);
		d.dimension       = GfxViewDimension::Texture2D;
		d.mostDetailedMip = mip;
		d.mipLevels       = 1;
		d.debugName       = "ResourceViewer";
		m_srv        = engine.createView(d);
		m_srvTexture = tex.handle.id;
		m_srvMip     = mip;
		if (!m_srv.valid())
			return void(m_status = "テクスチャのビューを作れません");
	}

	const uint32_t tw = std::max(1u, tex.width >> mip);
	const uint32_t th = std::max(1u, tex.height >> mip);
	// 縦横比を保ってビューポートに収める
	const float scale = std::min(static_cast<float>(w) / static_cast<float>(tw), static_cast<float>(h) / static_cast<float>(th));
	const float rw = tw * scale, rh = th * scale;
	const float rx = std::floor((w - rw) * 0.5f), ry = std::floor((h - rh) * 0.5f);

	const bool depth = isDepthFormat(tex.format);
	ViewConstants vc;
	vc.rect[0]  = rx; vc.rect[1] = ry; vc.rect[2] = rw; vc.rect[3] = rh;
	vc.range[0] = m_settings.rangeMin;
	vc.range[1] = m_settings.rangeMax;
	vc.range[2] = std::exp2(m_settings.exposure);
	vc.range[3] = (depth && m_settings.depthLinear) ? 1.0f : 0.0f;
	vc.depth[0] = zNear; vc.depth[1] = zFar; vc.depth[2] = m_settings.gamma ? 1.0f : 0.0f; vc.depth[3] = 0;
	vc.info[0]  = tw; vc.info[1] = th; vc.info[2] = m_settings.channels & 0xF; vc.info[3] = 0;
	if (vc.info[2] == 0)
		vc.info[2] = 0x7;

	cb->beginEvent("ResourceViewer");
	cb->barrier(gfxTransition(gfxRef(tex.handle), GfxResourceState::AllShaderResource));
	cb->setRenderTargets(1, &rt, nullptr, false, false);
	GfxViewport vp;
	vp.width  = static_cast<float>(w);
	vp.height = static_cast<float>(h);
	cb->setViewports(1, &vp);
	GfxRect sc;
	sc.right  = static_cast<int32_t>(w);
	sc.bottom = static_cast<int32_t>(h);
	cb->setScissors(1, &sc);
	cb->bindPipeline(m_viewPipe);
	cb->setRootConstants(0, 16, &vc);
	const GfxDescriptor srv = gfxView(m_srv);
	cb->setDescriptorTable(1, &srv, 1);
	cb->drawInstanced(3);

	// ---- カーソルの下のピクセル (固定したテクセルがあればそれ) ----
	const bool pinned = m_settings.pinX >= 0 && m_settings.pinY >= 0;
	if (pinned || (cursorX >= 0 && cursorY >= 0))
	{
		const float fx = pinned ? static_cast<float>(m_settings.pinX) : (cursorX - rx) / scale;
		const float fy = pinned ? static_cast<float>(m_settings.pinY) : (cursorY - ry) / scale;
		if (fx >= 0 && fy >= 0 && fx < tw && fy < th)
		{
			Slot& slot = m_slots[m_frame % m_slots.size()];
			const uint32_t pos[4] = { static_cast<uint32_t>(fx), static_cast<uint32_t>(fy), 0, 0 };
			cb->barrier(gfxTransition(gfxRef(m_probeBuffer), GfxResourceState::UnorderedAccess));
			cb->bindPipeline(m_probePipe);
			cb->setRootConstants(0, 4, pos);
			cb->setDescriptorTable(1, &srv, 1);
			const GfxDescriptor uav = gfxView(m_probeUav);
			cb->setDescriptorTable(2, &uav, 1);
			cb->dispatch(1, 1, 1);
			cb->copyBuffer(slot.readback, 0, m_probeBuffer, 0, 16);
			slot.pending        = true;
			slot.frame          = m_frame;
			slot.probe.valid    = true;
			slot.probe.resource = m_settings.resource;
			slot.probe.x        = pos[0];
			slot.probe.y        = pos[1];
			slot.probe.mip      = mip;
			slot.probe.depth    = depth;
			m_zNear = zNear;
			m_zFar  = zFar;
		}
	}
	cb->endEvent();
}

ResourceViewer::Probe ResourceViewer::takeProbe(GraphicsEngine& engine)
{
	Probe best;
	uint64_t bestFrame = m_lastProbeFrame;
	for (auto& s : m_slots)
	{
		if (!s.pending)
			continue;
		float v[4];
		if (!engine.readBuffer(s.readback, 0, sizeof(v), v))
			continue;   // まだ GPU が終わっていない
		s.pending = false;
		if (s.frame <= bestFrame)
			continue;
		bestFrame  = s.frame;
		best       = s.probe;
		std::memcpy(best.value.data(), v, sizeof(v));
		if (best.depth)
			best.distance = m_zNear * m_zFar / (m_zFar - v[0] * (m_zFar - m_zNear));
	}
	if (best.valid)
		m_lastProbeFrame = bestFrame;
	return best;
}
} // namespace studio
