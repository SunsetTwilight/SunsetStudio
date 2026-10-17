#pragma once
// ============================================================
//  editor/src/app/EditorApp.h
//
//  エディター全体の寿命。UI スレッド (main) で WebHost を動かし、描画スレッドでエンジンを動かす。
//  JS から呼べるメソッド (Bridge) と、JS に送るイベントをここで登録する。
//
//  M2a のメソッド:  ui.ready / app.info / app.ping
//  M2a のイベント:  log (エンジンとアプリのメッセージ) / engine.status (エンジンの初期化の結果)
// ============================================================

#include "host/WebHost.h"
#include "render/RenderThread.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>

namespace studio
{
// 起動時の設定 (コマンドラインと環境変数から作る)
struct EditorOptions
{
	std::filesystem::path uiIndex;   // 配布用の UI (<exe>/ui/index.html)
	std::string uiDevUrl;            // STUDIO_UI_DEV_URL (Vite の開発サーバー)
	bool        devTools  = false;   // STUDIO_DEVTOOLS=1 / Debug ビルド
	bool        software  = false;   // --software / STUDIO_ADAPTER=software (WARP)
	bool        smokeTest = false;   // --smoke-test: UI とエンジンが動いたら終了する (自動テスト用)
	int         smokeTimeoutSec = 60;

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

	// UI の log パネルと stderr に出す (どのスレッドからでもよい)
	void log(const char* level, const char* source, const std::string& text, int32_t id = 0);

	void checkSmokeTest();   // UI スレッドから呼ぶ

	EditorOptions m_options;
	WebHost       m_host;
	RenderThread  m_render;
	std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

	std::mutex                 m_engineMutex;
	EngineStatus               m_engineStatus = EngineStatus::Starting;
	RenderThread::InitResult   m_engineInfo;

	// スモークテストの進み具合 (UI スレッドだけが触る)
	bool m_uiReady       = false;
	bool m_infoRequested = false;
	bool m_engineDone    = false;
	bool m_engineOk      = false;
	bool m_smokeFinished = false;
	int  m_exitCode      = 0;
};
} // namespace studio
