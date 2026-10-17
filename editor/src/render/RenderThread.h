#pragma once
// ============================================================
//  editor/src/render/RenderThread.h
//
//  エンジン (GraphicsEngine) を持つスレッド。エンジンの API はこのスレッドからだけ呼ぶ。
//  UI スレッドは post() で処理を頼むだけで、完了を同期的に待たない
//  (Windows ではスワップチェーンの作成や Present がウィンドウのスレッドにメッセージを送ることがあり、
//   UI スレッドが描画スレッドを待つとデッドロックし得る。stop() はメッセージを処理しながら待つ)。
//  結果は、頼んだ処理の中から WebHost::post / Bridge::Reply で UI スレッドへ返す。
//
//  ビューポート (M2b): attachViewport したウィンドウが見えていて大きさがあるあいだ、
//  毎フレーム (vsync) シーンを描く。見えていない / 最小化のあいだは描かずに待つ。
//  M3d: シーンはレンダーグラフ (ViewportRenderer) で描き、アセットの変更を毎フレーム反映する。
//  M4a: シーンはプロジェクトを開いてから作る (openProject)。開いていなければ描かない。
// ============================================================

#include "OrbitCamera.h"
#include "ViewportRenderer.h"
#include "viewport/ViewportWindow.h"

#include "gfx/core/GraphicsEngine.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace studio
{
class RenderThread
{
public:
	using Task = std::function<void(GraphicsEngine&)>;

	struct Config
	{
		DeviceConfig                       device;
	};

	struct InitResult
	{
		bool        ok = false;
		std::string error;
		std::string adapter;
		GraphicsAPI api = GraphicsAPI::Auto;
	};

	struct FrameStats
	{
		double   fps     = 0;   // 直近の区間の平均
		double   frameMs = 0;
		double   cpuMs   = 0;   // 記録と submit にかかった時間 (Present の待ちを除く)
		uint32_t width   = 0;
		uint32_t height  = 0;
		uint64_t frames  = 0;   // 起動してから描いたフレーム数
		std::vector<gfx::rg::PassTiming> passes;   // パスごとの GPU 時間 (数フレーム前のもの)
		double   gpuMs   = 0;   // グラフ全体の GPU 時間
	};

	// 以下のコールバックはすべて描画スレッドから呼ばれる
	struct Callbacks
	{
		std::function<void(GfxMessageSeverity, int32_t id, const std::string& text)> message;   // エンジンのデバッグメッセージ
		std::function<void(const InitResult&)>                                       init;      // エンジンの初期化の結果
		std::function<void(const FrameStats&)>                                       stats;     // 約 0.5 秒ごと
		std::function<void(const std::string& level, const std::string& text)>       log;       // ビューポートの状態など
		std::function<void(uint64_t revision)>                                       graphChanged;   // グラフが切り替わった (読み直し / 編集)
	};

	RenderThread() = default;
	~RenderThread() { stop(); }
	RenderThread(const RenderThread&)            = delete;
	RenderThread& operator=(const RenderThread&) = delete;

	void start(Config config, Callbacks callbacks);

	// 描画スレッドで task を実行する (順番どおり)。エンジンの初期化に失敗していても呼ばれる
	// (task の中で engine.isInitialized() を確認すること)。stop 後は何もしない
	void post(Task task);
	// 描画スレッドでシーンを使う処理を実行する。シーンを作れていなければ scene は nullptr
	using SceneTask = std::function<void(GraphicsEngine&, ViewportRenderer* scene)>;
	void postScene(SceneTask task)
	{
		post([this, task = std::move(task)](GraphicsEngine& engine) { task(engine, m_sceneOk ? &m_scene : nullptr); });
	}

	// プロジェクトのアセットでシーンを作り直す (前のシーンとシェーダーは破棄する)。
	// done は描画スレッドから呼ばれる (ok = false なら error に理由。シーンはない状態になる)
	void openProject(std::filesystem::path assetRoot, std::string graphFile, std::function<void(bool ok, const std::string& error)> done);
	// シーンを破棄する (プロジェクトを閉じる)。done は描画スレッドから呼ばれる
	void closeProject(std::function<void()> done);

	// 待っている描画スレッドを起こす (ビューポートが見えるようになったときなど)
	void wake() { m_cv.notify_one(); }

	// ビューポートのウィンドウに描き始める。window は detachViewport の done が呼ばれるまで有効にしておくこと
	void attachViewport(ViewportWindow* window);
	// スワップチェーンを破棄し、描画スレッドから done を呼ぶ (そのあとでウィンドウを破棄してよい)
	void detachViewport(std::function<void()> done);

	// 残っている処理を実行してからエンジンを終了し、スレッドの終了を待つ (待つあいだ UI のメッセージを処理する)
	void stop();

	bool running() const { return m_thread.joinable(); }

private:
	void threadMain(Config config, Callbacks callbacks);
	bool canRender() const;
	void renderFrame(GraphicsEngine& engine);
	void releaseViewport(GraphicsEngine& engine);
	void pollAssets();
	void closeScene(GraphicsEngine& engine);
public:
	// グラフが切り替わっていたら graphChanged を呼ぶ (描画スレッドから。編集の処理のあとにも呼ぶ)
	void notifyGraph();
private:

	std::thread             m_thread;
	std::mutex              m_mutex;
	std::condition_variable m_cv;
	std::deque<Task>        m_tasks;
	bool                    m_stopping = false;

	// ---- 描画スレッドだけが触る ----
	Callbacks       m_callbacks;
	std::unique_ptr<ShaderLibrary> m_shaders;   // プロジェクトごとに作り直す (検索パスを持ち越さない)
	ViewportRenderer m_scene;
	uint64_t        m_graphRevision = 0;
	bool            m_sceneOk  = false;
	ViewportWindow* m_viewport = nullptr;
	RenderWindow*   m_window   = nullptr;
	uint32_t        m_windowW  = 0, m_windowH = 0;
	OrbitCamera     m_camera;

	uint64_t        m_frames = 0;
	uint64_t        m_statFrames = 0;
	double          m_statCpuMs  = 0;
	std::chrono::steady_clock::time_point m_statStart = std::chrono::steady_clock::now();
};
} // namespace studio
