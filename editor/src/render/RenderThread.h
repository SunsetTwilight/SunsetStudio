#pragma once
// ============================================================
//  editor/src/render/RenderThread.h
//
//  エンジン (GraphicsEngine) を持つスレッド。エンジンの API はこのスレッドからだけ呼ぶ。
//  UI スレッドは post() で処理を頼むだけで、完了を同期的に待たない
//  (Windows では Present がウィンドウのスレッドにメッセージを送ることがあり、待つとデッドロックし得る)。
//  結果は、頼んだ処理の中から WebHost::post / Bridge::Reply で UI スレッドへ返す。
//
//  M2a: エンジンの初期化 / 終了と、頼まれた処理の実行だけ。フレームのループ (ビューポートの描画) は M2b。
// ============================================================

#include "gfx/core/GraphicsEngine.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace studio
{
class RenderThread
{
public:
	using Task = std::function<void(GraphicsEngine&)>;

	struct InitResult
	{
		bool        ok = false;
		std::string error;
		std::string adapter;
		GraphicsAPI api = GraphicsAPI::Auto;
	};

	// メッセージはエンジンのデバッグメッセージ (シェーダーのエラーなど)。描画スレッドから呼ばれる
	using MessageFn = std::function<void(GfxMessageSeverity severity, int32_t id, const std::string& text)>;
	using InitFn    = std::function<void(const InitResult& result)>;

	RenderThread() = default;
	~RenderThread() { stop(); }
	RenderThread(const RenderThread&)            = delete;
	RenderThread& operator=(const RenderThread&) = delete;

	// スレッドを起動し、そのスレッドでエンジンを初期化する。onInit は描画スレッドから呼ばれる
	void start(DeviceConfig config, MessageFn onMessage, InitFn onInit);

	// 描画スレッドで task を実行する (順番どおり)。エンジンの初期化に失敗していても呼ばれる
	// (task の中で engine.isInitialized() を確認すること)。stop 後は何もしない
	void post(Task task);

	// 残っている処理を実行してからエンジンを終了し、スレッドの終了を待つ
	void stop();

	bool running() const { return m_thread.joinable(); }

private:
	void threadMain(DeviceConfig config, MessageFn onMessage, InitFn onInit);

	std::thread             m_thread;
	std::mutex              m_mutex;
	std::condition_variable m_cv;
	std::deque<Task>        m_tasks;
	bool                    m_stopping = false;
};
} // namespace studio
