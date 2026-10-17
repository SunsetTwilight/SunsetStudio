// ============================================================
//  editor/src/render/SceneRenderer.cpp
// ============================================================
#include "SceneRenderer.h"

#include <cmath>

namespace studio
{
namespace
{
// editor/shaders/common.hlsli の Frame と合わせる
struct FrameConstants
{
	Mat4  viewProj;
	float cameraPos[4];
	float lightDir[4];
	float viewportSize[4];
};

struct Vertex
{
	float pos[3];
	float normal[3];
};

// 面ごとに法線の違う 24 頂点の立方体 (-0.5〜0.5)
void makeCube(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices)
{
	const float n[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	for (int f = 0; f < 6; ++f)
	{
		const Float3 nn{ n[f][0], n[f][1], n[f][2] };
		// 法線に垂直な 2 軸 (u × v = n になる向き)
		const Float3 u = std::fabs(nn.y) > 0.5f ? Float3{ 1, 0, 0 } : normalize(cross({ 0, 1, 0 }, nn));
		const Float3 v = cross(nn, u);
		const uint16_t base = static_cast<uint16_t>(vertices.size());
		const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
		for (const auto& c : corners)
		{
			const Float3 p = nn * 0.5f + u * (c[0] * 0.5f) + v * (c[1] * 0.5f);
			vertices.push_back({ { p.x, p.y, p.z }, { nn.x, nn.y, nn.z } });
		}
		// 反時計回りが表 (frontCounterClockwise = true)
		static const uint16_t order[] = { 0, 1, 2, 0, 2, 3 };
		for (uint16_t i : order)
			indices.push_back(static_cast<uint16_t>(base + i));
	}
}
} // namespace

bool SceneRenderer::init(GraphicsEngine& engine, ShaderLibrary& shaders, std::string& error)
{
	const ShaderHandle sceneVS = shaders.load("scene.vs.hlsl", ShaderStage::Vertex);
	const ShaderHandle scenePS = shaders.load("scene.ps.hlsl", ShaderStage::Fragment);
	const ShaderHandle gridVS  = shaders.load("grid.vs.hlsl",  ShaderStage::Vertex);
	const ShaderHandle gridPS  = shaders.load("grid.ps.hlsl",  ShaderStage::Fragment);
	if (!sceneVS.valid() || !scenePS.valid() || !gridVS.valid() || !gridPS.valid())
	{
		error = "ビューポートのシェーダーを作れませんでした: " + shaders.lastError();
		return false;
	}

	// [0] b0: フレームの定数 (ルート CBV)、[1] b1: オブジェクトの定数 (ルート定数 20 個)
	GfxBindingLayoutDesc layoutDesc;
	layoutDesc.addRootDescriptor(GfxRootParameterType::CBV, 0, 0, GfxShaderVisibility::All);
	layoutDesc.addConstants(1, 20, 0, GfxShaderVisibility::All);
	layoutDesc.flags     = GfxRootSignatureFlags::AllowInputAssemblerInputLayout;
	layoutDesc.debugName = "ViewportLayout";
	m_layout = engine.createBindingLayout(layoutDesc);

	GfxGraphicsPipelineDesc scene;
	scene.bindingLayout                 = m_layout;
	scene.vs                            = sceneVS;
	scene.ps                            = scenePS;
	scene.rasterizerState.cullMode      = GfxCullMode::Back;
	scene.rasterizerState.frontCounterClockwise = true;
	scene.depthStencilState.depthFunc   = GfxCompareFunc::Less;
	scene.inputLayout.add("POSITION", 0, GfxFormat::R32G32B32_Float)
	                 .add("NORMAL",   0, GfxFormat::R32G32B32_Float);
	scene.numRenderTargets              = 1;
	scene.rtvFormats[0]                 = GfxFormat::B8G8R8A8_Unorm;
	scene.dsvFormat                     = GfxFormat::D32_Float;
	scene.debugName                     = "ViewportScene";
	m_scenePipe = engine.createPipeline(scene);

	GfxGraphicsPipelineDesc grid = scene;
	grid.vs                          = gridVS;
	grid.ps                          = gridPS;
	grid.inputLayout                 = {};
	grid.rasterizerState.cullMode    = GfxCullMode::None;
	grid.depthStencilState.depthFunc = GfxCompareFunc::LessEqual;
	grid.depthStencilState.depthWriteMask = GfxDepthWriteMask::Zero;
	GfxRenderTargetBlendDesc& blend  = grid.blendState.renderTarget[0];
	blend.blendEnable    = true;
	blend.srcBlend       = GfxBlend::SrcAlpha;
	blend.destBlend      = GfxBlend::InvSrcAlpha;
	blend.srcBlendAlpha  = GfxBlend::One;
	blend.destBlendAlpha = GfxBlend::InvSrcAlpha;
	grid.debugName       = "ViewportGrid";
	m_gridPipe = engine.createPipeline(grid);

	if (!m_layout.valid() || !m_scenePipe.valid() || !m_gridPipe.valid())
	{
		error = "ビューポートのパイプラインを作れませんでした (詳細はログ)";
		return false;
	}

	// ---- メッシュと定数バッファ ----
	std::vector<Vertex>   vertices;
	std::vector<uint16_t> indices;
	makeCube(vertices, indices);
	m_vb = engine.createBuffer({ vertices.size() * sizeof(Vertex), BufferUsage::Vertex, vertices.data(), sizeof(Vertex) });
	m_ib = engine.createBuffer({ indices.size() * sizeof(uint16_t), BufferUsage::Index, indices.data(), sizeof(uint16_t) });
	m_frameCB = engine.createBuffer({ 256, BufferUsage::Uniform });
	if (!m_vb.valid() || !m_ib.valid() || !m_frameCB.valid())
	{
		error = "ビューポートのバッファを作れませんでした";
		return false;
	}

	// ---- 並べる立方体 ----
	const float palette[5][3] = {
		{ 0.80f, 0.36f, 0.20f }, { 0.25f, 0.55f, 0.80f }, { 0.35f, 0.70f, 0.40f },
		{ 0.85f, 0.75f, 0.30f }, { 0.60f, 0.45f, 0.75f },
	};
	for (int z = -3; z <= 3; ++z)
		for (int x = -3; x <= 3; ++x)
		{
			if (x == 0 && z == 0)
				continue;
			const float h = 0.6f + 1.4f * (0.5f + 0.5f * std::sin(x * 1.3f + z * 0.7f));
			ObjectConstants o;
			o.world = Mat4::translateScale({ x * 3.0f, h * 0.5f, z * 3.0f }, { 1.4f, h, 1.4f });
			const float* c = palette[(x + z + 12) % 5];
			o.color[0] = c[0]; o.color[1] = c[1]; o.color[2] = c[2]; o.color[3] = 1;
			m_objects.push_back(o);
		}
	ObjectConstants center;
	center.world = Mat4::translateScale({ 0, 1.5f, 0 }, { 3, 3, 3 });
	center.color[0] = 0.92f; center.color[1] = 0.55f; center.color[2] = 0.30f; center.color[3] = 1;
	m_objects.push_back(center);
	return true;
}

void SceneRenderer::shutdown(GraphicsEngine& engine)
{
	if (m_depth) engine.destroyDepthStencil(m_depth);
	m_depth = nullptr;
	engine.destroyPipeline(m_scenePipe);
	engine.destroyPipeline(m_gridPipe);
	engine.destroyBindingLayout(m_layout);
	engine.destroyBuffer(m_vb);
	engine.destroyBuffer(m_ib);
	engine.destroyBuffer(m_frameCB);
	m_scenePipe = m_gridPipe = {};
	m_layout = {};
	m_vb = m_ib = m_frameCB = {};
	m_objects.clear();
}

void SceneRenderer::ensureDepth(GraphicsEngine& engine, uint32_t width, uint32_t height)
{
	if (m_depth && m_depthW == width && m_depthH == height)
		return;
	if (m_depth)
		engine.destroyDepthStencil(m_depth);   // GPU が使い終わってから解放される
	m_depth  = engine.createDepthStencil({ width, height, PixelFormat::D32_FLOAT, 1, 1.0f, 0, "ViewportDepth" });
	m_depthW = width;
	m_depthH = height;
}

void SceneRenderer::render(GraphicsEngine& engine, IRenderTarget* rt, uint32_t width, uint32_t height, const OrbitCamera& camera)
{
	if (!rt || width == 0 || height == 0 || !m_scenePipe.valid())
		return;
	ensureDepth(engine, width, height);
	if (!m_depth)
		return;

	FrameConstants fc;
	fc.viewProj = camera.projection(static_cast<float>(width) / static_cast<float>(height)) * camera.view();
	const Float3 eye = camera.eye();
	fc.cameraPos[0] = eye.x; fc.cameraPos[1] = eye.y; fc.cameraPos[2] = eye.z; fc.cameraPos[3] = 1;
	const Float3 light = normalize({ -0.45f, -1.0f, -0.30f });
	fc.lightDir[0] = light.x; fc.lightDir[1] = light.y; fc.lightDir[2] = light.z; fc.lightDir[3] = 0;
	fc.viewportSize[0] = static_cast<float>(width);
	fc.viewportSize[1] = static_cast<float>(height);
	fc.viewportSize[2] = 1.0f / static_cast<float>(width);
	fc.viewportSize[3] = 1.0f / static_cast<float>(height);
	engine.updateBuffer(m_frameCB, &fc, sizeof(fc));

	ICommandBuffer* cb = engine.allocCommandBuffer();
	if (!cb)
		return;
	cb->begin();
	cb->beginEvent("Viewport");
	cb->setRenderTargets(1, &rt, m_depth, false, true);
	const float clear[4] = { 0.075f, 0.078f, 0.088f, 1.0f };
	cb->clearRenderTarget(rt, clear);

	// 立方体
	cb->bindPipeline(m_scenePipe);
	cb->bindUniformBuffer(m_frameCB, 0);
	const GfxVertexBufferBinding vb{ m_vb, 0, 0, sizeof(Vertex) };
	cb->setVertexBuffers(0, 1, &vb);
	GfxIndexBufferBinding ib;
	ib.buffer = m_ib;
	ib.format = GfxFormat::R16_Uint;
	cb->setIndexBuffer(ib);
	for (const ObjectConstants& o : m_objects)
	{
		cb->setRootConstants(1, 20, &o);
		cb->drawIndexedInstanced(36);
	}

	// 地面のグリッド (半透明。立方体のあとに深度テストだけで描く)
	cb->bindPipeline(m_gridPipe);
	cb->bindUniformBuffer(m_frameCB, 0);
	cb->drawInstanced(6);

	cb->endEvent();
	cb->end();
	engine.submitCommandBuffers({ cb });
	cb->release();
}
} // namespace studio
