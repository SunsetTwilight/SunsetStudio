// ============================================================
//  editor/src/render/RenderThread.cpp
// ============================================================
#include "RenderThread.h"

#include "gfx/core/ShaderLibrary.h"

#include <windows.h>

namespace studio
{
void RenderThread::start(Config config, Callbacks callbacks)
{
	if (m_thread.joinable())
		return;
	{
		std::lock_guard lock(m_mutex);
		m_stopping = false;
	}
	m_thread = std::thread(&RenderThread::threadMain, this, std::move(config), std::move(callbacks));
}

void RenderThread::post(Task task)
{
	{
		std::lock_guard lock(m_mutex);
		if (m_stopping)
			return;
		m_tasks.push_back(std::move(task));
	}
	m_cv.notify_one();
}

void RenderThread::attachViewport(ViewportWindow* window)
{
	post([this, window](GraphicsEngine&) {
		m_viewport = window;
		if (m_callbacks.log)
			m_callbacks.log("info", "ビューポートを接続しました");
	});
}

void RenderThread::postAlways(Task task, std::function<void()> fallback)
{
	bool posted = false;
	{
		std::lock_guard lock(m_mutex);
		if (m_thread.joinable())
		{
			// 止めている途中でも積む (スレッドが終わったあとなら実行されないが、そのときはアプリの終了処理が片付ける)
			m_tasks.push_back(std::move(task));
			posted = true;
		}
	}
	if (posted)
		m_cv.notify_one();
	else if (fallback)
		fallback();   // スレッドは動いていない (エンジンもない)
}

void RenderThread::detachViewport(std::function<void()> done)
{
	postAlways(
		[this, done](GraphicsEngine& engine) {
			releaseViewport(engine);
			m_viewport = nullptr;
			if (done)
				done();
		},
		[this, done] {
			m_viewport = nullptr;
			if (done)
				done();
		});
}

void RenderThread::resetViewport(std::function<void()> done)
{
	postAlways(
		[this, done](GraphicsEngine& engine) {
			releaseViewport(engine);
			if (done)
				done();
		},
		done);
}

void RenderThread::addPreview(uint32_t id, FloatingWindow* window, const ResourceViewer::Settings& settings)
{
	post([this, id, window, settings](GraphicsEngine&) {
		Preview p;
		p.id       = id;
		p.window   = window;
		p.settings = settings;
		m_previews.push_back(std::move(p));
	});
}

void RenderThread::setPreview(uint32_t id, const ResourceViewer::Settings& settings)
{
	post([this, id, settings](GraphicsEngine&) {
		for (auto& p : m_previews)
			if (p.id == id)
			{
				p.settings = settings;
				p.reported = false;
			}
	});
}

void RenderThread::removePreview(uint32_t id, std::function<void()> done)
{
	postAlways(
		[this, id, done](GraphicsEngine& engine) {
			for (auto it = m_previews.begin(); it != m_previews.end(); ++it)
				if (it->id == id)
				{
					ResourceViewer::releaseCache(engine, it->cache);
					if (it->swap)
						engine.destroyWindow(it->swap);
					m_previews.erase(it);
					break;
				}
			if (done)
				done();
		},
		[this, id, done] {
			std::erase_if(m_previews, [id](const Preview& p) { return p.id == id; });
			if (done)
				done();
		});
}

void RenderThread::stop()
{
	if (!m_thread.joinable())
		return;
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
	}
	m_cv.notify_one();

	// 描画スレッドがスワップチェーンを破棄するとき、ウィンドウのスレッド (このスレッド) にメッセージを送ることがある。
	// 待つあいだもメッセージを処理する
	HANDLE h = static_cast<HANDLE>(m_thread.native_handle());
	while (MsgWaitForMultipleObjects(1, &h, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
	{
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	m_thread.join();
}

bool RenderThread::viewportDrawable() const
{
	if (!m_viewport || !m_viewport->visible())
		return false;
	if (m_viewport->width() == 0 || m_viewport->height() == 0)
		return false;
	// トップレベルのウィンドウ (メイン、または別ウィンドウ) が最小化されているあいだは描かない
	return !IsIconic(static_cast<HWND>(m_viewport->parentHandle()));
}

bool RenderThread::anyPreviewDrawable() const
{
	for (const auto& p : m_previews)
		if (p.window && p.window->drawable())
			return true;
	return false;
}

bool RenderThread::canRender() const
{
	return m_sceneOk && (viewportDrawable() || anyPreviewDrawable());
}

void RenderThread::releaseViewport(GraphicsEngine& engine)
{
	if (m_window)
	{
		engine.destroyWindow(m_window);
		m_window = nullptr;   // 大きさ (m_windowW / H) は、見えないあいだのオフスクリーンの大きさに使う
	}
}

// プレビューのウィンドウごとに、グラフのリソースを描いて Present する
void RenderThread::renderPreviews(GraphicsEngine& engine)
{
	for (auto& p : m_previews)
	{
		if (!p.window || !p.window->drawable())
			continue;
		const uint32_t w = p.window->width(), h = p.window->height();
		if (!p.swap)
		{
			WindowDesc wd;
			wd.nativeWindowHandle = p.window->nativeHandle();
			wd.width       = w;
			wd.height      = h;
			wd.vsync       = false;   // 待つのはビューポートの Present だけ
			wd.format      = PixelFormat::BGRA8_UNORM;
			wd.bufferCount = 2;
			wd.name        = "Preview";
			p.swap = engine.createWindow(wd);
			if (!p.swap)
			{
				p.status = "スワップチェーンを作れません: " + engine.lastError();
				if (m_callbacks.previewStatus)
					m_callbacks.previewStatus(p.id, p.status);
				p.window = nullptr;   // 作り直そうとしない (閉じれば消える)
				continue;
			}
			p.w = w;
			p.h = h;
		}
		else if ((w != p.w || h != p.h) && p.swap->resize(w, h))
		{
			p.w = w;
			p.h = h;
		}
		if (!p.swap->acquireNextBuffer())
			continue;
		std::string status;
		m_scene.drawPreview(engine, p.swap->getRenderTarget(), p.w, p.h, p.settings, p.cache, m_camera, status);
		p.swap->present();
		if (!p.reported || status != p.status)
		{
			p.status   = status;
			p.reported = true;
			if (m_callbacks.previewStatus)
				m_callbacks.previewStatus(p.id, status);
		}
	}
}

// 保存されたアセットを反映し、結果と新しいエラーをログに出す
void RenderThread::pollAssets()
{
	m_shaders->poll();   // .hlsl (使っているパイプラインはエンジンが作り直す)
	const ViewportRenderer::PollResult r = m_scene.pollAssets();
	auto log = [this](const char* level, const std::string& text) {
		if (m_callbacks.log)
			m_callbacks.log(level, text);
	};
	for (const auto& p : r.reloadedPipelines) log("info", "パイプラインを作り直しました: " + p);
	for (const auto& p : r.failedPipelines)   log("warning", "パイプラインを作り直せませんでした (元のまま): " + p);
	for (const auto& m : r.graphMessages)     log(m.rfind("error", 0) == 0 ? "error" : "warning", "グラフ: " + m);
	if (r.graphReloaded)   log("info", "グラフを読み直しました: " + m_scene.graphFile());
	if (r.graphRecompiled) log("info", "グラフをコンパイルし直しました");
	if (r.graphFailed)     log("warning", "グラフを読み直せませんでした (元のまま): " + m_scene.graphFile());
	for (const auto& e : m_scene.takeNewErrors())
		log("error", e);
	notifyGraph();
}

void RenderThread::notifyGraph()
{
	const uint64_t rev = m_scene.graph().revision();
	if (rev != m_graphRevision)
	{
		m_graphRevision = rev;
		if (m_callbacks.graphChanged)
			m_callbacks.graphChanged(rev);
	}
}

void RenderThread::renderFrame(GraphicsEngine& engine)
{
	const bool     toViewport = viewportDrawable();
	IRenderTarget* target     = nullptr;
	uint32_t       w = 0, h = 0;

	if (toViewport)
	{
		w = m_viewport->width();
		h = m_viewport->height();
		// ---- スワップチェーン (最初に見えたときに作り、大きさが変わったら合わせる) ----
		if (!m_window)
		{
			WindowDesc wd;
			wd.nativeWindowHandle = m_viewport->nativeHandle();
			wd.width       = w;
			wd.height      = h;
			wd.vsync       = true;
			wd.format      = PixelFormat::BGRA8_UNORM;
			wd.bufferCount = 3;
			wd.name        = "Viewport";
			m_window = engine.createWindow(wd);
			if (!m_window)
			{
				if (m_callbacks.log)
					m_callbacks.log("error", "ビューポートのスワップチェーンを作れませんでした: " + engine.lastError());
				m_sceneOk = false;   // 何度も作り直そうとしない
				return;
			}
			m_windowW = w;
			m_windowH = h;
		}
		else if (w != m_windowW || h != m_windowH)
		{
			if (m_window->resize(w, h))
			{
				m_windowW = w;
				m_windowH = h;
			}
		}
		w = m_windowW;
		h = m_windowH;
	}
	else
	{
		// ビューポートは見えない (タブの裏、最小化) がプレビューはある: 最後のビューポートの大きさでオフスクリーンに描く
		w = m_windowW ? m_windowW : 1280;
		h = m_windowH ? m_windowH : 720;
		if (m_offscreen && (m_offscreenW != w || m_offscreenH != h))
		{
			engine.destroyRenderTarget(m_offscreen);
			m_offscreen = nullptr;
		}
		if (!m_offscreen)
		{
			RenderTargetDesc rd;
			rd.width      = w;
			rd.height     = h;
			rd.format     = PixelFormat::BGRA8_UNORM;
			rd.shaderRead = true;
			rd.debugName  = "OffscreenBackbuffer";
			m_offscreen  = engine.createRenderTarget(rd);
			m_offscreenW = w;
			m_offscreenH = h;
			if (!m_offscreen)
			{
				if (m_callbacks.log)
					m_callbacks.log("error", "オフスクリーンの描画先を作れませんでした: " + engine.lastError());
				m_sceneOk = false;
				return;
			}
		}
		target = m_offscreen;
	}

	const auto t0 = std::chrono::steady_clock::now();

	// 保存されたアセット (シェーダー、パイプライン、グラフ) を反映する
	pollAssets();

	int cx = -1, cy = -1;
	if (toViewport)
	{
		m_camera.apply(m_viewport->takeInput(), static_cast<float>(h));
		if (!m_window->acquireNextBuffer())
			return;
		target = m_window->getRenderTarget();
		if (!m_viewport->cursor(cx, cy))
			cx = cy = -1;
	}
	m_scene.render(engine, target, w, h, m_camera, cx, cy);

	// リソースビューアーのピクセルの値と状態 (約 10 回 / 秒)
	if (m_scene.viewerOk() && m_scene.viewer().settings().enabled)
	{
		const ResourceViewer::Probe p = m_scene.viewer().takeProbe(engine);
		if (p.valid)
			m_lastProbe = p;
		const auto now = std::chrono::steady_clock::now();
		if (now - m_viewerReport >= std::chrono::milliseconds(100) && m_callbacks.viewer)
		{
			m_viewerReport = now;
			const bool pinned = m_scene.viewer().settings().pinX >= 0;
			m_callbacks.viewer(cx >= 0 || pinned ? m_lastProbe : ResourceViewer::Probe{}, m_scene.viewer().status());
		}
	}
	renderPreviews(engine);
	const auto t1 = std::chrono::steady_clock::now();
	if (toViewport)
		m_window->present();   // vsync で待つ
	else
		std::this_thread::sleep_for(std::chrono::milliseconds(15));   // vsync の代わり (回しすぎない)

	// ---- 統計 ----
	++m_frames;
	++m_statFrames;
	m_statCpuMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
	const auto   now     = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration<double>(now - m_statStart).count();
	if (elapsed >= 0.5)
	{
		if (m_callbacks.stats)
		{
			FrameStats s;
			s.fps     = m_statFrames / elapsed;
			s.frameMs = elapsed * 1000.0 / static_cast<double>(m_statFrames);
			s.cpuMs   = m_statCpuMs / static_cast<double>(m_statFrames);
			s.width   = w;
			s.height  = h;
			s.frames  = m_frames;
			s.passes  = m_scene.executor().timings();
			s.gpuMs   = m_scene.executor().totalGpuMs();
			m_callbacks.stats(s);
		}
		m_statStart  = now;
		m_statFrames = 0;
		m_statCpuMs  = 0;
	}
}

void RenderThread::closeScene(GraphicsEngine& engine)
{
	for (auto& p : m_previews)
		ResourceViewer::releaseCache(engine, p.cache);
	if (m_offscreen)
	{
		engine.destroyRenderTarget(m_offscreen);
		m_offscreen  = nullptr;
		m_offscreenW = m_offscreenH = 0;
	}
	if (m_sceneOk || m_shaders)
	{
		m_scene.shutdown(engine);
		if (m_shaders)
			m_shaders->clear();
		m_shaders.reset();
	}
	m_sceneOk       = false;
	m_graphRevision = 0;
}

void RenderThread::openProject(std::filesystem::path assetRoot, std::string graphFile, std::function<void(bool, const std::string&)> done)
{
	post([this, assetRoot = std::move(assetRoot), graphFile = std::move(graphFile), done = std::move(done)](GraphicsEngine& engine) {
		closeScene(engine);
		if (!engine.isInitialized())
		{
			if (done) done(false, "エンジンが初期化されていません");
			return;
		}
		m_shaders = std::make_unique<ShaderLibrary>(engine);
		m_shaders->setOnReload([this](ShaderHandle, const std::string& path, bool ok) {
			if (m_callbacks.shaderReloaded)
				m_callbacks.shaderReloaded(path, ok);
			if (m_callbacks.log)
				m_callbacks.log(ok ? "info" : "warning",
					ok ? "シェーダーを作り直しました: " + path : "シェーダーを作り直せませんでした (元のまま): " + path);
		});
		std::string error;
		m_sceneOk = m_scene.init(engine, *m_shaders, assetRoot, graphFile, error);
		for (const auto& e : m_scene.takeNewErrors())
			if (m_callbacks.log) m_callbacks.log("error", e);
		if (!m_sceneOk)
			closeScene(engine);
		else
		{
			m_camera.reset();
			notifyGraph();
		}
		if (done) done(m_sceneOk, error);
	});
}

void RenderThread::closeProject(std::function<void()> done)
{
	post([this, done = std::move(done)](GraphicsEngine& engine) {
		closeScene(engine);
		if (done) done();
	});
}

void RenderThread::threadMain(Config config, Callbacks callbacks)
{
	m_callbacks = std::move(callbacks);
	GraphicsEngine engine;

	// デバッグメッセージ (エンジンの API を呼んだこのスレッドから呼ばれる)
	if (m_callbacks.message)
		config.device.debugMessage = [this](GfxMessageSeverity sev, int32_t id, const char* text) {
			m_callbacks.message(sev, id, text ? text : "");
		};

	InitResult result;
	result.ok = engine.init(config.device);
	if (result.ok)
	{
		result.adapter = engine.adapterName();
		result.api     = engine.currentAPI();
	}
	else
		result.error = engine.lastError();
	if (m_callbacks.init)
		m_callbacks.init(result);

	// ビューポートのシーンは、プロジェクトを開いたときに作る (openProject)

	// ---- メインループ: 頼まれた処理を実行し、ビューポートが見えていれば 1 フレーム描く ----
	for (;;)
	{
		std::deque<Task> tasks;
		{
			std::unique_lock lock(m_mutex);
			if (!m_stopping && m_tasks.empty() && !canRender())
				m_cv.wait_for(lock, std::chrono::milliseconds(100));   // 最小化の解除なども拾えるよう、ときどき起きる
			tasks.swap(m_tasks);
			if (m_stopping && tasks.empty())
				break;
		}
		for (auto& task : tasks)
			task(engine);

		if (canRender())
			renderFrame(engine);
	}

	releaseViewport(engine);
	for (auto& p : m_previews)
		if (p.swap)
			engine.destroyWindow(p.swap);
	closeScene(engine);
	m_previews.clear();
	engine.shutdown();
}
} // namespace studio
