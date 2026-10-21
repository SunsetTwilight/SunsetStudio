// ============================================================
//  editor/src/render/ViewportRenderer.cpp
// ============================================================
#include "ViewportRenderer.h"

#include "services/ProjectAssets.h"

#include <algorithm>
#include <cmath>

namespace studio
{
namespace
{
const char* k_ShadowPass = "Shadow";   // 影のパスの drawList の layer (描画レイヤーによらず、影を落とす物体を描く)
const char* k_GridLayer  = "Grid";     // エディターのグリッド (物体は描かない)

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
	Mat4     world;
	float    color[4];
	uint32_t lightMask[4];   // x = ライトリンクのマスク (M5-5。G-Buffer に書く)
};

// 面ごとに法線の違う 24 頂点の立方体 (-0.5〜0.5)。UV は面ごとに 0〜1、接線は u の向き
void makeCube(std::vector<MeshVertex>& vertices, std::vector<uint16_t>& indices)
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
			// UV は左上が原点 (v は下向き)。接線 = UV の u の向き、従法線 (n × t) = UV の v が減る向き
			vertices.push_back({ { p.x, p.y, p.z }, { nn.x, nn.y, nn.z }, { u.x, u.y, u.z, -1 },
			                     { (c[0] + 1) * 0.5f, 1 - (c[1] + 1) * 0.5f } });
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
	m_unboundReported.clear();
	m_pipelines = std::make_unique<gfx::rg::PipelineLibrary>(engine, shaders);
	m_pipelines->setRoot(assetRoot);
	m_executor  = std::make_unique<gfx::rg::Executor>(engine, *m_pipelines);
	m_options.bindingLayout = [this](const std::string& p) { return m_pipelines->bindingLayout(p); };

	// ライトの種類 (M5-5): Lighting シェーダーが include する shaders/generated/lights.hlsli を先に書く
	{
		std::string lightError;
		if (!m_lights.init(engine, assetRoot, lightError))
			m_viewerError += (m_viewerError.empty() ? "" : "\n") + lightError;
	}

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
	std::vector<MeshVertex> vertices;
	std::vector<uint16_t>   indices;
	makeCube(vertices, indices);
	m_vb = engine.createBuffer({ vertices.size() * sizeof(MeshVertex), BufferUsage::Vertex, vertices.data(), sizeof(MeshVertex) });
	m_ib = engine.createBuffer({ indices.size() * sizeof(uint16_t), BufferUsage::Index, indices.data(), sizeof(uint16_t) });
	m_frameCB = engine.createBuffer({ sizeof(FrameConstants), BufferUsage::Uniform });
	m_shadowFrameCB = engine.createBuffer({ sizeof(FrameConstants), BufferUsage::Uniform });
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
		std::string overlayError;
		m_unassignedOk = m_unassigned.init(engine, overlayError);
		if (!m_unassignedOk)
			m_viewerError += (m_viewerError.empty() ? "" : "\n") + overlayError;
	}
	m_engine = &engine;
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
	m_unassigned.shutdown(engine);
	m_unassignedOk = false;
	m_sceneDesc = {};
	m_selection.clear();
	for (auto& o : m_objects)
		releaseDraws(engine, o, 0);
	m_objects.clear();
	m_models.clear(engine);
	m_lights.shutdown(engine);
	m_engine = nullptr;
	engine.destroyBuffer(m_vb);
	engine.destroyBuffer(m_ib);
	engine.destroyBuffer(m_frameCB);
	if (m_shadowFrameCB.valid())
		engine.destroyBuffer(m_shadowFrameCB);
	m_shadowFrameCB = {};
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
	// ライトの種類の変更
	m_lights.poll();
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
	for (auto& e : m_lights.takeErrors())
		out.push_back(std::move(e));
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
	// 最初の平行光源の向き (ライトの定数を使わない古いシェーダー用。ライトがなければ今までの向き)
	fc.lightDir[0] = m_sunDirection.x; fc.lightDir[1] = m_sunDirection.y; fc.lightDir[2] = m_sunDirection.z; fc.lightDir[3] = 0;
	fc.viewportSize[0] = static_cast<float>(width);
	fc.viewportSize[1] = static_cast<float>(height);
	fc.viewportSize[2] = 1.0f / static_cast<float>(width);
	fc.viewportSize[3] = 1.0f / static_cast<float>(height);
	engine.updateBuffer(m_frameCB, &fc, sizeof(fc));
	// 影のパス (view "shadow") のフレームの定数: 光の視点の view × projection
	{
		FrameConstants sc = fc;
		sc.viewProj    = m_shadowViewProj;
		sc.invViewProj = Mat4::inverse(m_shadowViewProj);
		if (m_shadowFrameCB.valid())
			engine.updateBuffer(m_shadowFrameCB, &sc, sizeof(sc));
	}

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
	// どの描画ノードにも入らないメッシュ (レイヤーが空か、グラフにないレイヤーだけ) をピンクで重ねる
	if (m_unassignedOk && !(m_viewerOk && m_viewer.settings().enabled))
	{
		std::vector<std::string> graphLayers;
		for (const auto& p : m_graph->desc().passes)
			if (p.enabled && p.content.kind == gfx::rg::ContentKind::DrawList && p.content.drawList.layer != k_ShadowPass &&
			    p.content.drawList.layer != k_GridLayer)
				graphLayers.push_back(p.content.drawList.layer);
		std::vector<UnassignedOverlay::Mesh> pink;
		for (const Object& o : m_objects)
			if (o.visible && !o.light)
				for (const DrawItem& d : o.draws)
					if (std::none_of(d.layers.begin(), d.layers.end(), [&](const std::string& l) {
						    return std::find(graphLayers.begin(), graphLayers.end(), l) != graphLayers.end(); }))
						pink.push_back({ d.vb, d.ib, d.indexFormat, d.indexCount, d.indexOffset, d.world });
		m_unassigned.record(engine, cb, rt, width, height, fc.viewProj, pink);
	}
	// ライトの印 (薄い黄) と、選んでいる物体の印 (橙)。リソースビューアーで別のものを表示しているときは描かない
	if (m_outlineOk && !(m_viewerOk && m_viewer.settings().enabled))
	{
		static const float lightColor[4] = { 0.95f, 0.85f, 0.35f, 1.0f };
		for (const Object& o : m_objects)
			if (o.light && o.visible && o.id != m_selection)
				m_outline.record(cb, rt, width, height, fc.viewProj, o.box, lightColor);
		if (!m_selection.empty())
			for (const Object& o : m_objects)
				if (o.id == m_selection && o.visible)
					m_outline.record(cb, rt, width, height, fc.viewProj, o.box);
	}
	if (m_viewerOk)
		m_viewer.record(engine, cb, *m_executor, rt, width, height, cursorX, cursorY, camera.zNear(), camera.zFar());
	cb->end();
	engine.submitCommandBuffers({ cb });
	cb->release();
}

void ViewportRenderer::setScene(GraphicsEngine& engine, const SceneDesc& scene)
{
	m_sceneDesc = scene;
	updateObjects(engine);
}

void ViewportRenderer::setModel(GraphicsEngine& engine, const std::string& id, const sunset::asset::Model* model)
{
	if (model)
	{
		std::string error;
		if (!m_models.set(engine, id, *model, error))
			m_viewerError += (m_viewerError.empty() ? "" : "\n") + error;
	}
	else
		m_models.remove(engine, id);
	updateObjects(engine);
}

void ViewportRenderer::releaseDraws(GraphicsEngine& engine, Object& o, size_t keep)
{
	while (o.draws.size() > keep)
	{
		if (o.draws.back().cb.valid())
			engine.destroyBuffer(o.draws.back().cb);
		o.draws.pop_back();
	}
}

void ViewportRenderer::updateObjects(GraphicsEngine& engine)
{
	const SceneDesc&          scene   = m_sceneDesc;
	const std::vector<Matrix> world   = worldMatrices(scene);
	const std::vector<bool>   visible = effectiveVisibility(scene);
	const LightLinks          links   = computeLightLinks(scene);
	std::vector<LightSystem::Light> lights;
	bool sunFound = false;
	const SceneLight*    shadowLight = nullptr;
	std::array<float, 3> shadowDir {};
	// 定数バッファは使い回し、足りなければ作り、余れば消す (エンジンが GPU の使い終わりまで解放を遅らせる)
	while (m_objects.size() > scene.objects.size())
	{
		releaseDraws(engine, m_objects.back(), 0);
		m_objects.pop_back();
	}
	m_objects.resize(scene.objects.size());
	for (size_t i = 0; i < scene.objects.size(); ++i)
	{
		const SceneObject& so = scene.objects[i];
		Object& o = m_objects[i];
		o.id      = so.id;
		o.visible = visible[i];
		std::copy(world[i].begin(), world[i].end(), o.world.m);
		o.box   = o.world;
		o.light = so.light.has_value();
		o.castShadow = so.castShadow;

		// ライト (M5-5): 描かず、印の箱 (0.5) と、ライトの定数に入れる
		if (so.light)
		{
			releaseDraws(engine, o, 0);
			o.box = o.world * Mat4::translateScale({ 0, 0, 0 }, { 0.5f, 0.5f, 0.5f });
			if (visible[i])
			{
				const auto d = lightDirection(world[i]);
				LightSystem::Light l;
				l.type      = so.light->type;
				l.position  = { o.world.m[12], o.world.m[13], o.world.m[14] };
				l.direction = { d[0], d[1], d[2] };
				l.params    = so.light->params;
				l.mask      = links.lightMasks[i];
				const LightTypeDef* t = m_lights.type(so.light->type);
				const bool directional = t && (t->shape == "directional" || t->id == "directional");
				// 影: 影を落とす設定の、最初の平行光源 (今はこれだけ)
				if (directional && so.light->shadow.enabled && !shadowLight)
				{
					l.shadow      = true;
					shadowLight   = &*so.light;
					shadowDir     = d;
				}
				lights.push_back(std::move(l));
				if (!sunFound && directional)
				{
					m_sunDirection = { d[0], d[1], d[2] };
					sunFound = true;
				}
			}
			continue;
		}

		// 描くもの: 立方体は 1 つ (スロット "Default")、モデルは (ノード, サブメッシュ) ごと
		struct Want
		{
			BufferHandle vb, ib;
			GfxFormat    format = GfxFormat::R32_Uint;
			uint32_t     count = 0, offset = 0;
			Mat4         world;
			std::vector<std::string> layers;
		};
		std::vector<Want> wants;
		if (so.model == "builtin:cube")
			wants.push_back({ m_vb, m_ib, GfxFormat::R16_Uint, 36, 0, o.world, drawLayersOf(so, "Default") });
		else if (const auto modelId = assetIdOfModel(so.model))
		{
			if (const ModelCache::Gpu* gpu = m_models.find(*modelId))
			{
				o.box = o.world * gpu->box;
				for (const ModelCache::Draw& d : gpu->draws)
				{
					const std::string slot = d.slot < gpu->slotNames.size() ? gpu->slotNames[d.slot] : std::string();
					const ModelCache::Mesh& mesh = gpu->meshes[d.mesh];
					wants.push_back({ mesh.vb, mesh.ib, GfxFormat::R32_Uint, d.indexCount, d.indexOffset, o.world * d.node,
					                  drawLayersOf(so, slot) });
				}
			}
		}
		releaseDraws(engine, o, wants.size());
		o.draws.resize(wants.size());
		for (size_t k = 0; k < wants.size(); ++k)
		{
			DrawItem&   d = o.draws[k];
			const Want& w = wants[k];
			d.vb          = w.vb;
			d.ib          = w.ib;
			d.indexFormat = w.format;
			d.indexCount  = w.count;
			d.indexOffset = w.offset;
			d.layers      = w.layers;
			d.world       = w.world;
			if (!d.cb.valid())
				d.cb = engine.createBuffer({ sizeof(ObjectConstants), BufferUsage::Uniform });
			ObjectConstants c;
			c.world = w.world;
			std::copy(so.color.begin(), so.color.end(), c.color);
			c.lightMask[0] = links.objectMasks[i];
			c.lightMask[1] = c.lightMask[2] = c.lightMask[3] = 0;
			if (d.cb.valid())
				engine.updateBuffer(d.cb, &c, sizeof(c));
		}
	}
	if (!sunFound)
		m_sunDirection = normalize({ -0.45f, -1.0f, -0.30f });
	for (const auto& w : links.warnings)
		m_viewerError += (m_viewerError.empty() ? "" : "\n") + w;
	m_lights.setLights(std::move(lights));
	// 影の行列: 影を落とす (描くものがある) 物体全体の箱に合わせる
	m_shadowActive = shadowLight != nullptr;
	if (shadowLight)
	{
		std::vector<Matrix> boxes;
		for (size_t i = 0; i < m_objects.size(); ++i)
		{
			const Object& o = m_objects[i];
			if (o.visible && !o.light && o.castShadow && !o.draws.empty())
			{
				Matrix b;
				std::copy(o.box.m, o.box.m + 16, b.begin());
				boxes.push_back(b);
			}
		}
		const Matrix m = directionalShadowMatrix(shadowDir, boxes);
		std::copy(m.begin(), m.end(), m_shadowViewProj.m);
		m_lights.setShadow(m_shadowViewProj, shadowLight->shadow.bias, shadowLight->shadow.normalBias, shadowLight->shadow.softness);
	}
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
	// 判定は物体ごとの箱 (立方体はそのもの、モデルは境界の箱)
	std::vector<Matrix> world;
	for (const Object& o : m_objects)
	{
		Matrix m;
		std::copy(o.box.m, o.box.m + 16, m.begin());
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
	if (query.layer == k_GridLayer)
	{
		ctx.cb->drawInstanced(6);   // y = 0 の大きな四角形 (assets/shaders/grid.vs.hlsl)
		return;
	}
	if (!m_engine)
		return;
	// 物体 (スロット) の描画レイヤーにこの layer があるメッシュを、パスのパイプラインで描く
	// (パイプラインとフレームなどの定数は Executor がつないでいる。ここでは物体の定数と頂点 / インデックスだけ)。
	// 影のパス: 描画レイヤーによらず、影を落とす物体だけ
	const std::string& layer = query.layer;
	const bool shadowPass = layer == k_ShadowPass;
	if (shadowPass && !m_shadowActive)
		return;   // 影を落とすライトがない
	// パイプラインのルートパラメーターで、パス (定数 / 入出力の bind) がつないでいないものがあれば描かない。
	// つながないまま描くと、GPU が初期化していない値を読む (マテリアルのころのパイプラインの b2 / t0 など)
	if (const GfxBindingLayoutDesc* bl = m_pipelines->bindingLayout(ctx.pass->desc.pipeline))
	{
		std::vector<bool> bound(bl->numParameters, false);
		auto mark = [&](int32_t p) { if (p >= 0 && p < static_cast<int32_t>(bound.size())) bound[static_cast<size_t>(p)] = true; };
		for (const auto& c : ctx.pass->desc.constants) mark(c.bind.param);
		for (const auto& r : ctx.pass->desc.inputs)    mark(r.bind.param);
		for (const auto& r : ctx.pass->desc.outputs)   mark(r.bind.param);
		for (uint32_t i = 0; i < bl->numParameters; ++i)
			if (!bound[i])
			{
				if (m_unboundReported.insert(ctx.pass->desc.pipeline + "#" + std::to_string(i)).second)
					m_viewerError += (m_viewerError.empty() ? "" : "\n") + std::string("パス ") + ctx.pass->name + " のパイプライン " +
					                 ctx.pass->desc.pipeline + " のルートパラメーター " + std::to_string(i) +
					                 " をつないでいないので、メッシュを描きません (マテリアルのころのパイプラインなら、ファイル > マテリアルなしの描画に移す)";
				return;
			}
	}
	const int32_t objectParam = ctx.paramOf("object");
	uint64_t lastVb = 0, lastIb = 0;
	for (const Object& o : m_objects)
	{
		if (!o.visible || (shadowPass && !o.castShadow))
			continue;
		for (const DrawItem& d : o.draws)
		{
			if (!shadowPass && std::find(d.layers.begin(), d.layers.end(), layer) == d.layers.end())
				continue;
			if (d.vb.id != lastVb)
			{
				const GfxVertexBufferBinding vb{ d.vb, 0, 0, sizeof(MeshVertex) };
				ctx.cb->setVertexBuffers(0, 1, &vb);
				lastVb = d.vb.id;
			}
			if (d.ib.id != lastIb)
			{
				GfxIndexBufferBinding ib;
				ib.buffer = d.ib;
				ib.format = d.indexFormat;
				ctx.cb->setIndexBuffer(ib);
				lastIb = d.ib.id;
			}
			if (objectParam >= 0)
				ctx.cb->setRootDescriptor(static_cast<uint32_t>(objectParam), d.cb, 0);
			ctx.cb->drawIndexedInstanced(d.indexCount, 1, d.indexOffset, 0, 0);
		}
	}
}

BufferHandle ViewportRenderer::constants(const std::string& source, const gfx::rg::DrawContext& ctx)
{
	if (source == "frame")
		return ctx.view() == "shadow" ? m_shadowFrameCB : m_frameCB;
	if (source == "lights" && m_engine)   // M5-5: シーンのライト (cbuffer Lights)
		return m_lights.constants(*m_engine);
	return {};   // "object" は draw の中でオブジェクトごとにつなぐ
}
} // namespace studio
