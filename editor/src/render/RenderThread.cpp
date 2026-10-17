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

void RenderThread::detachViewport(std::function<void()> done)
{
	bool posted = false;
	{
		std::lock_guard lock(m_mutex);
		if (!m_stopping)
		{
			m_tasks.push_back([this, done](GraphicsEngine& engine) {
				releaseViewport(engine);
				m_viewport = nullptr;
				if (done)
					done();
			});
			posted = true;
		}
	}
	if (posted)
		m_cv.notify_one();
	else if (done)
		done();   // スレッドはもう止まっている
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

bool RenderThread::canRender() const
{
	if (!m_sceneOk || !m_viewport || !m_viewport->visible())
		return false;
	if (m_viewport->width() == 0 || m_viewport->height() == 0)
		return false;
	// トップレベルのウィンドウが最小化されているあいだは描かない
	return !IsIconic(static_cast<HWND>(m_viewport->parentHandle()));
}

void RenderThread::releaseViewport(GraphicsEngine& engine)
{
	if (m_window)
	{
		engine.destroyWindow(m_window);
		m_window  = nullptr;
		m_windowW = m_windowH = 0;
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
	if (r.graphReloaded)   log("info", "グラフを読み直しました: " + std::string(ViewportRenderer::k_GraphFile));
	if (r.graphRecompiled) log("info", "グラフをコンパイルし直しました");
	if (r.graphFailed)     log("warning", "グラフを読み直せませんでした (元のまま): " + std::string(ViewportRenderer::k_GraphFile));
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
	const uint32_t w = m_viewport->width();
	const uint32_t h = m_viewport->height();

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

	const auto t0 = std::chrono::steady_clock::now();

	// 保存されたアセット (シェーダー、パイプライン、グラフ) を反映する
	pollAssets();

	m_camera.apply(m_viewport->takeInput(), static_cast<float>(m_windowH));

	if (!m_window->acquireNextBuffer())
		return;
	m_scene.render(engine, m_window->getRenderTarget(), m_windowW, m_windowH, m_camera);
	const auto t1 = std::chrono::steady_clock::now();
	m_window->present();   // vsync で待つ

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
			s.width   = m_windowW;
			s.height  = m_windowH;
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

	// ---- ビューポートのシーン ----
	ShaderLibrary shaders(engine);
	m_shaders = &shaders;
	shaders.setOnReload([this](ShaderHandle, const std::string& path, bool ok) {
		if (m_callbacks.log)
			m_callbacks.log(ok ? "info" : "warning",
				ok ? "シェーダーを作り直しました: " + path : "シェーダーを作り直せませんでした (元のまま): " + path);
	});
	if (result.ok)
	{
		std::string error;
		m_sceneOk = m_scene.init(engine, shaders, config.assetRoot, error);
		if (!m_sceneOk && m_callbacks.log)
			m_callbacks.log("error", error);
		if (m_sceneOk)
		{
			for (const auto& e : m_scene.takeNewErrors())
				if (m_callbacks.log) m_callbacks.log("error", e);
			notifyGraph();
		}
	}

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
	m_scene.shutdown(engine);
	shaders.clear();
	m_shaders = nullptr;
	engine.shutdown();
}
} // namespace studio
