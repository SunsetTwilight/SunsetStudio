// ============================================================
//  editor/src/render/RenderThread.cpp
// ============================================================
#include "RenderThread.h"

namespace studio
{
void RenderThread::start(DeviceConfig config, MessageFn onMessage, InitFn onInit)
{
	if (m_thread.joinable())
		return;
	{
		std::lock_guard lock(m_mutex);
		m_stopping = false;
	}
	m_thread = std::thread(&RenderThread::threadMain, this, std::move(config), std::move(onMessage), std::move(onInit));
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

void RenderThread::stop()
{
	if (!m_thread.joinable())
		return;
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
	}
	m_cv.notify_one();
	m_thread.join();
}

void RenderThread::threadMain(DeviceConfig config, MessageFn onMessage, InitFn onInit)
{
	GraphicsEngine engine;

	// デバッグメッセージ (エンジンの API を呼んだこのスレッドから呼ばれる)
	if (onMessage)
		config.debugMessage = [onMessage](GfxMessageSeverity sev, int32_t id, const char* text) {
			onMessage(sev, id, text ? text : "");
		};

	InitResult result;
	result.ok = engine.init(config);
	if (result.ok)
	{
		result.adapter = engine.adapterName();
		result.api     = engine.currentAPI();
	}
	else
		result.error = engine.lastError();
	if (onInit)
		onInit(result);

	// 頼まれた処理を順に実行する。M2b でここにフレームのループ (ビューポートの描画) が入る
	for (;;)
	{
		Task task;
		{
			std::unique_lock lock(m_mutex);
			m_cv.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
			if (m_tasks.empty())
				break;   // m_stopping かつ空
			task = std::move(m_tasks.front());
			m_tasks.pop_front();
		}
		task(engine);
	}

	engine.shutdown();
}
} // namespace studio
