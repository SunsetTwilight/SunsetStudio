#pragma once
// ============================================================
//  editor/src/app/EditorApp.h
//
//  エディター全体の寿命。UI スレッド (main) で WebHost とビューポートのウィンドウを動かし、
//  描画スレッドでエンジンを動かす。JS から呼べるメソッド (Bridge) と、JS に送るイベントをここで登録する。
//
//  メソッド:  ui.ready / app.info / app.ping / viewport.setRect /
//             schema.types / schema.describe / pipeline.list / pipeline.get / pipeline.update
//  イベント:  log (エンジンとアプリのメッセージ) / engine.status (エンジンの初期化の結果) / stats (フレーム時間)
// ============================================================

#include "host/WebHost.h"
#include "render/RenderThread.h"
#include "viewport/ViewportWindow.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace studio
{
// 起動時の設定 (コマンドラインと環境変数から作る)
struct EditorOptions
{
	std::filesystem::path              uiIndex;      // 配布用の UI (<exe>/ui/index.html)
	std::vector<std::filesystem::path> shaderDirs;   // ビューポートのシェーダー (開発時はソースのフォルダを先に)
	std::string uiDevUrl;            // STUDIO_UI_DEV_URL (Vite の開発サーバー)
	bool        devTools  = false;   // STUDIO_DEVTOOLS=1 / Debug ビルド
	bool        software  = false;   // --software / STUDIO_ADAPTER=software (WARP)
	bool        smokeTest = false;   // --smoke-test: UI とエンジンとビューポートが動いたら終了する (自動テスト用)
	int         smokeTimeoutSec = 60;
	uint64_t    smokeFrames     = 30;   // スモークテストで描けたことにするフレーム数

	static EditorOptions fromEnvironment(int argc, char** argv);
};

class EditorApp
{
public:
	explicit EditorApp(EditorOptions options) : m_options(std::move(options)) {}

	// ウィンドウを閉じるまで戻らない。戻り値はプロセスの終了コード
	int run();

private:
	enum class EngineStatus { Starting, Ready, Failed };

	void registerMethods();
	Json engineJson();   // m_engineMutex を取ってから呼ぶこと
	void onEngineInit(const RenderThread::InitResult& result);
	void onEngineMessage(GfxMessageSeverity severity, int32_t id, const std::string& text);
	void onStats(const RenderThread::FrameStats& stats);

	// UI の log パネルと stderr に出す (どのスレッドからでもよい)
	void log(const char* level, const char* source, const std::string& text, int32_t id = 0);

	void checkSmokeTest();   // UI スレッドから呼ぶ
	void runInspectorSelfCheck(std::function<void(bool ok, const std::string& error)> done);

	EditorOptions  m_options;
	WebHost        m_host;       // 破棄の順: 描画スレッド → ビューポート → WebView
	ViewportWindow m_viewport;
	RenderThread   m_render;
	std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

	std::mutex               m_engineMutex;
	EngineStatus             m_engineStatus = EngineStatus::Starting;
	RenderThread::InitResult m_engineInfo;

	std::atomic<uint32_t> m_d3dProblems { 0 };   // D3D12 のエラー / 警告の数 (スモークテストで確認する)

	// スモークテストの進み具合 (UI スレッドだけが触る)
	bool     m_uiReady       = false;
	bool     m_infoRequested = false;
	bool     m_engineDone    = false;
	bool     m_engineOk      = false;
	uint64_t m_framesDrawn   = 0;
	enum class SmokeStage { WaitFrames, Inspector, WaitAfter, Finished };
	SmokeStage m_smokeStage    = SmokeStage::WaitFrames;
	uint64_t   m_smokeFrameMark = 0;
	int      m_exitCode      = 0;
};
} // namespace studio
