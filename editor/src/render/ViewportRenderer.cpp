// ============================================================
//  editor/src/render/ViewportRenderer.cpp
// ============================================================
#include "ViewportRenderer.h"

#include <algorithm>
#include <cmath>

namespace studio
{
namespace
{
// assets/shaders/common.hlsli の Frame と合わせる
struct FrameConstants
{
	Mat4  viewProj;
	Mat4  invViewProj;
	float cameraPos[4];
	float lightDir[4];
	float viewportSize[4];
};

// assets/shaders/common.hlsli の Object と合わせる
struct ObjectConstants
{
	Mat4  world;
	float color[4];
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
		// 反時計回りが表 (pipelines/gbuffer.json の frontCounterClockwise = true)
		static const uint16_t order[] = { 0, 1, 2, 0, 2, 3 };
		for (uint16_t i : order)
			indices.push_back(static_cast<uint16_t>(base + i));
	}
}
} // namespace

bool ViewportRenderer::init(GraphicsEngine& engine, ShaderLibrary& shaders, const std::filesystem::path& assetRoot,
                            const std::string& graphFile, std::string& error)
{
	m_root      = assetRoot;
	m_graphFile = graphFile;
	m_reportedPipelineErrors = m_reportedExecutorErrors = 0;
	m_pipelines = std::make_unique<gfx::rg::PipelineLibrary>(engine, shaders);
	m_pipelines->setRoot(assetRoot);
	m_executor  = std::make_unique<gfx::rg::Executor>(engine, *m_pipelines);
	m_options.bindingLayout = [this](const std::string& p) { return m_pipelines->bindingLayout(p); };

	m_graph = std::make_unique<gfx::rg::GraphAsset>(assetRoot / gfx::rg::pathFromUtf8(graphFile));
	if (!m_graph->load(m_options))
	{
		error = "ビューポートのグラフ (" + graphFile + ") を読めませんでした";
		for (const auto& m : m_graph->messages())
			error += "\n" + m;
		return false;
	}

	// ---- リソースビューアー (作れなくてもビューポートは描く) ----
	{
		std::string viewerError;
		m_viewerOk = m_viewer.init(engine, viewerError);
		if (!m_viewerOk)
			m_viewerError = viewerError;
	}

	// ---- メッシュと定数バッファ ----
	std::vector<Vertex>   vertices;
	std::vector<uint16_t> indices;
	makeCube(vertices, indices);
	m_vb = engine.createBuffer({ vertices.size() * sizeof(Vertex), BufferUsage::Vertex, vertices.data(), sizeof(Vertex) });
	m_ib = engine.createBuffer({ indices.size() * sizeof(uint16_t), BufferUsage::Index, indices.data(), sizeof(uint16_t) });
	m_frameCB = engine.createBuffer({ sizeof(FrameConstants), BufferUsage::Uniform });
	if (!m_vb.valid() || !m_ib.valid() || !m_frameCB.valid())
	{
		error = "ビューポートのバッファを作れませんでした";
		return false;
	}

	// ---- 選択の印 (作れなくてもビューポートは描く) ----
	{
		std::string outlineError;
		m_outlineOk = m_outline.init(engine, outlineError);
		if (!m_outlineOk)
			m_viewerError += (m_viewerError.empty() ? "" : "\n") + outlineError;
	}
	// 物体はシーンのファイルから (setScene)
	return true;
}

void ViewportRenderer::shutdown(GraphicsEngine& engine)
{
	if (m_executor)  m_executor->clear();
	if (m_pipelines) m_pipelines->clear();
	m_executor.reset();
	m_pipelines.reset();
	m_graph.reset();
	m_viewer.shutdown(engine);
	m_viewerOk = false;
	m_outline.shutdown(engine);
	m_outlineOk = false;
	m_sceneDesc = {};
	m_selection.clear();
	for (auto& o : m_objects)
		engine.destroyBuffer(o.cb);
	m_objects.clear();
	engine.destroyBuffer(m_vb);
	engine.destroyBuffer(m_ib);
	engine.destroyBuffer(m_frameCB);
	m_vb = m_ib = m_frameCB = {};
}

ViewportRenderer::PollResult ViewportRenderer::pollAssets()
{
	PollResult r;
	const gfx::rg::PipelineLibrary::PollResult pr = m_pipelines->poll();
	r.reloadedPipelines = pr.reloaded;
	r.failedPipelines   = pr.failed;
	r.graphReloaded     = m_graph->poll(m_options);
	r.graphFailed       = m_graph->lastFailed();
	// パイプラインのレイアウトが変わると bind の検証の結果も変わるので、コンパイルし直す
	if (!r.graphReloaded && !r.graphFailed && !pr.reloaded.empty())
	{
		r.graphRecompiled = m_graph->recompile(m_options);
		r.graphFailed     = m_graph->lastFailed();
	}
	if (r.graphReloaded || r.graphRecompiled || r.graphFailed)
		r.graphMessages = m_graph->messages();
	m_graph->clearStatus();
	return r;
}

std::vector<std::string> ViewportRenderer::takeNewErrors()
{
	std::vector<std::string> out;
	if (!m_viewerError.empty())
	{
		out.push_back(m_viewerError);
		m_viewerError.clear();
	}
	if (!m_pipelines || !m_executor)
		return out;
	const auto& pe = m_pipelines->errors();
	for (; m_reportedPipelineErrors < pe.size(); ++m_reportedPipelineErrors)
		out.push_back(pe[m_reportedPipelineErrors]);
	const auto& ee = m_executor->errors();
	for (; m_reportedExecutorErrors < ee.size(); ++m_reportedExecutorErrors)
		out.push_back(ee[m_reportedExecutorErrors]);
	return out;
}

std::vector<std::string> ViewportRenderer::pipelineFiles() const
{
	std::vector<std::string> out;
	std::error_code ec;
	for (const auto& e : std::filesystem::directory_iterator(m_root / "pipelines", ec))
		if (e.is_regular_file(ec) && e.path().extension() == ".json")
		{
			const std::u8string name = e.path().filename().u8string();
			out.push_back("pipelines/" + std::string(name.begin(), name.end()));
		}
	std::sort(out.begin(), out.end());
	return out;
}

void ViewportRenderer::render(GraphicsEngine& engine, IRenderTarget* rt, uint32_t width, uint32_t height, const OrbitCamera& camera,
                              int cursorX, int cursorY)
{
	if (!rt || width == 0 || height == 0 || !m_graph || !m_graph->valid())
		return;

	FrameConstants fc;
	fc.viewProj    = camera.projection(static_cast<float>(width) / static_cast<float>(height)) * camera.view();
	fc.invViewProj = Mat4::inverse(fc.viewProj);
	const Float3 eye = camera.eye();
	fc.cameraPos[0] = eye.x; fc.cameraPos[1] = eye.y; fc.cameraPos[2] = eye.z; fc.cameraPos[3] = 1;
	const Float3 light = normalize({ -0.45f, -1.0f, -0.30f });
	fc.lightDir[0] = light.x; fc.lightDir[1] = light.y; fc.lightDir[2] = light.z; fc.lightDir[3] = 0;
	fc.viewportSize[0] = static_cast<float>(width);
	fc.viewportSize[1] = static_cast<float>(height);
	fc.viewportSize[2] = 1.0f / static_cast<float>(width);
	fc.viewportSize[3] = 1.0f / static_cast<float>(height);
	engine.updateBuffer(m_frameCB, &fc, sizeof(fc));

	gfx::rg::FrameInputs in;
	in.width    = width;
	in.height   = height;
	in.provider = this;
	in.imports["$backbuffer"] = gfx::rg::ImportedResource::fromRenderTarget(rt);

	ICommandBuffer* cb = engine.allocCommandBuffer();
	if (!cb)
		return;
	cb->begin();
	m_executor->record(cb, m_graph->desc(), m_graph->compiled(), in);
	// 選んでいる物体の印 (リソースビューアーで別のものを表示しているときは描かない)
	if (m_outlineOk && !m_selection.empty() && !(m_viewerOk && m_viewer.settings().enabled))
		for (const Object& o : m_objects)
			if (o.id == m_selection && o.visible)
				m_outline.record(cb, rt, width, height, fc.viewProj, o.world);
	if (m_viewerOk)
		m_viewer.record(engine, cb, *m_executor, rt, width, height, cursorX, cursorY, camera.zNear(), camera.zFar());
	cb->end();
	engine.submitCommandBuffers({ cb });
	cb->release();
}

void ViewportRenderer::setScene(GraphicsEngine& engine, const SceneDesc& scene)
{
	const std::vector<Matrix> world   = worldMatrices(scene);
	const std::vector<bool>   visible = effectiveVisibility(scene);
	// 定数バッファは使い回し、足りなければ作り、余れば消す (エンジンが GPU の使い終わりまで解放を遅らせる)
	while (m_objects.size() > scene.objects.size())
	{
		engine.destroyBuffer(m_objects.back().cb);
		m_objects.pop_back();
	}
	m_objects.resize(scene.objects.size());
	for (size_t i = 0; i < scene.objects.size(); ++i)
	{
		const SceneObject& so = scene.objects[i];
		Object& o = m_objects[i];
		if (!o.cb.valid())
			o.cb = engine.createBuffer({ sizeof(ObjectConstants), BufferUsage::Uniform });
		o.id      = so.id;
		o.layer   = so.layer;
		o.visible = visible[i];
		o.cube    = so.model == "builtin:cube";
		std::copy(world[i].begin(), world[i].end(), o.world.m);
		ObjectConstants c;
		c.world = o.world;
		std::copy(so.color.begin(), so.color.end(), c.color);
		if (o.cb.valid())
			engine.updateBuffer(o.cb, &c, sizeof(c));
	}
	m_sceneDesc = scene;
}

std::string ViewportRenderer::pick(int x, int y, uint32_t width, uint32_t height, const OrbitCamera& camera) const
{
	if (width == 0 || height == 0 || x < 0 || y < 0)
		return {};
	const Mat4 viewProj = camera.projection(static_cast<float>(width) / static_cast<float>(height)) * camera.view();
	const Mat4 inv      = Mat4::inverse(viewProj);
	const float nx = (x + 0.5f) / width * 2.0f - 1.0f;
	const float ny = 1.0f - (y + 0.5f) / height * 2.0f;
	auto unproject = [&](float z) {
		const float v[4] = { nx, ny, z, 1 };
		float r[4] = {};
		for (int row = 0; row < 4; ++row)
			for (int k = 0; k < 4; ++k)
				r[row] += inv.m[k * 4 + row] * v[k];
		return std::array<float, 3>{ r[0] / r[3], r[1] / r[3], r[2] / r[3] };
	};
	const auto n = unproject(0.0f), f = unproject(1.0f);
	std::array<float, 3> dir = { f[0] - n[0], f[1] - n[1], f[2] - n[2] };
	const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
	if (len <= 0)
		return {};
	for (float& d : dir) d /= len;
	std::vector<Matrix> world;
	for (const Object& o : m_objects)
	{
		Matrix m;
		std::copy(o.world.m, o.world.m + 16, m.begin());
		world.push_back(m);
	}
	const int i = pickObject(m_sceneDesc, world, n, dir);
	return i >= 0 ? m_sceneDesc.objects[static_cast<size_t>(i)].id : std::string();
}

void ViewportRenderer::drawPreview(GraphicsEngine& engine, IRenderTarget* rt, uint32_t width, uint32_t height,
                                   const ResourceViewer::Settings& settings, ResourceViewer::ViewCache& cache,
                                   const OrbitCamera& camera, std::string& status)
{
	status.clear();
	if (!m_viewerOk)
		return void(status = "リソースビューアーを使えません (理由はログ)");
	if (!rt || width == 0 || height == 0 || !m_graph || !m_graph->valid())
		return;
	ICommandBuffer* cb = engine.allocCommandBuffer();
	if (!cb)
		return;
	cb->begin();
	cb->beginEvent("Preview");
	if (!m_viewer.draw(engine, cb, *m_executor, settings, cache, rt, width, height, camera.zNear(), camera.zFar(), status))
	{
		// 表示できないときは暗い色で塗る (前の画像を残さない)
		const float clear[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
		cb->setRenderTargets(1, &rt, nullptr, false, false);
		cb->clearRenderTarget(rt, clear);
	}
	cb->endEvent();
	cb->end();
	engine.submitCommandBuffers({ cb });
	cb->release();
}

void ViewportRenderer::draw(const gfx::rg::DrawListQuery& query, const gfx::rg::DrawContext& ctx)
{
	if (query.layer != "Grid")   // シーンの物体: layer が同じものを描く (M5-4 からはマテリアルパス)
	{
		const int32_t objectParam = ctx.paramOf("object");
		const GfxVertexBufferBinding vb{ m_vb, 0, 0, sizeof(Vertex) };
		ctx.cb->setVertexBuffers(0, 1, &vb);
		GfxIndexBufferBinding ib;
		ib.buffer = m_ib;
		ib.format = GfxFormat::R16_Uint;
		ctx.cb->setIndexBuffer(ib);
		for (const Object& o : m_objects)
		{
			if (!o.visible || !o.cube || o.layer != query.layer)
				continue;
			if (objectParam >= 0)
				ctx.cb->setRootDescriptor(static_cast<uint32_t>(objectParam), o.cb, 0);
			ctx.cb->drawIndexedInstanced(36);
		}
	}
	else if (query.layer == "Grid")
		ctx.cb->drawInstanced(6);   // y = 0 の大きな四角形 (assets/shaders/grid.vs.hlsl)
}

BufferHandle ViewportRenderer::constants(const std::string& source, const gfx::rg::DrawContext&)
{
	return source == "frame" ? m_frameCB : BufferHandle{};   // "object" は draw の中でオブジェクトごとにつなぐ
}
} // namespace studio
