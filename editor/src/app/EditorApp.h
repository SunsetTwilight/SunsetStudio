#pragma once
// ============================================================
//  editor/src/app/EditorApp.h
//
//  エディター全体の寿命。UI スレッド (main) で WebHost とビューポートのウィンドウを動かし、
//  描画スレッドでエンジンを動かす。JS から呼べるメソッド (Bridge) と、JS に送るイベントをここで登録する。
//
//  メソッド:  ui.ready / app.info / app.ping / viewport.setRect /
//             schema.types / schema.describe /
//             pipeline.list / pipeline.get / pipeline.update / pipeline.save   (パイプラインのアセット)
//             graph.get / graph.set / graph.save                               (ビューポートのレンダーグラフ)
//             project.info / project.recent / project.templates / project.open / project.create / project.close /
//             project.layout.get / project.layout.set / dialog.openProject / dialog.pickFolder   (プロジェクト、M4a)
//             edit.undo / edit.redo / edit.state                               (編集の履歴、M4)
//             viewer.get / viewer.set                                          (リソースビューアー、M4)
//             preview.open / preview.set / preview.setRect / preview.close / preview.list
//                                                                              (プレビュー: 別ウィンドウ / タブ、M4)
//             viewport.float / viewport.state                                  (ビューポートを別ウィンドウにする、M4)
//  イベント:  log (エンジンとアプリのメッセージ) / engine.status (エンジンの初期化の結果) /
//             stats (フレーム時間とパスごとの GPU 時間) / graph.changed (グラフが切り替わった) /
//             project.changed (プロジェクトを開いた / 閉じた) / edit.changed (履歴の状態) /
//             pipeline.changed (元に戻す / やり直すでパイプラインが変わった) /
//             viewer.pixel (リソースビューアーのピクセルの値と状態) /
//             preview.changed (プレビューの一覧) / viewport.changed (ビューポートが別ウィンドウか)
// ============================================================

#include "host/WebHost.h"
#include "services/Project.h"
#include "services/UndoStack.h"
#include "render/RenderThread.h"
#include "viewport/FloatingWindow.h"
#include "viewport/ViewportWindow.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace studio
{
// 起動時の設定 (コマンドラインと環境変数から作る)
struct EditorOptions
{
	std::filesystem::path              uiIndex;      // 配布用の UI (<exe>/ui/index.html)
	std::filesystem::path              templateRoot; // プロジェクトのテンプレートのフォルダ (開発時はソースの editor/templates)
	std::filesystem::path              projectFile;  // コマンドラインで渡されたプロジェクト (空なら起動画面)
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
	void registerProjectMethods();
	void registerEditMethods();
	void registerViewerMethods();
	void registerWindowMethods();   // EditorWindows.cpp
	// 書かれた項目だけを settings に入れる。値が不正なら false (error に理由)
	static bool applyViewerPatch(const Json& patch, ResourceViewer::Settings& settings, std::string& error);
	static Json viewerJson(const ResourceViewer::Settings& settings);

	// ---- 別ウィンドウ (EditorWindows.cpp、UI スレッド) ----
	Json previewsJson() const;
	void emitPreviews();
	void closePreview(uint32_t id);
	void closeAllPreviews();
	void onPreviewStatus(uint32_t id, const std::string& status);
	// ビューポートを別ウィンドウにする / メインのウィンドウに戻す。x / y はスクリーン座標 (物理ピクセル、< 0 なら OS に任せる)
	void floatViewport(bool floating, int x, int y, std::function<void(bool ok, const std::string& error)> done);
	// 終了の前に、別ウィンドウのスワップチェーンを解放してからウィンドウを壊す
	void releaseWindows(std::function<void()> done);
	void recordEdit(UndoStack::Entry entry);   // 描画スレッドから呼んでよい (UI スレッドで履歴に入れる)
	void emitEditState();                      // UI スレッドから呼ぶ
	Json projectJson() const;
	// プロジェクトを開く (UI スレッド)。結果は done に (UI スレッドから) 返す
	void openProject(const std::filesystem::path& file, std::function<void(bool ok, const Json& result, const std::string& error)> done);
	Json engineJson();   // m_engineMutex を取ってから呼ぶこと
	void onEngineInit(const RenderThread::InitResult& result);
	void onEngineMessage(GfxMessageSeverity severity, int32_t id, const std::string& text);
	void onStats(const RenderThread::FrameStats& stats);

	// UI の log パネルと stderr に出す (どのスレッドからでもよい)
	void log(const char* level, const char* source, const std::string& text, int32_t id = 0);

	void checkSmokeTest();   // UI スレッドから呼ぶ
	void runInspectorSelfCheck(std::function<void(bool ok, const std::string& error)> done);
	void runGraphSelfCheck(std::function<void(bool ok, const std::string& error)> done);

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
	enum class SmokeStage { WaitProject, WaitFrames, Inspector, Graph, Viewer, Windows, WaitAfter, Finished };
	SmokeStage m_smokeStage    = SmokeStage::WaitProject;
	bool       m_smokeProjectFailed = false;
	std::filesystem::path m_smokeDir;   // スモークテストのプロジェクト (一時フォルダ。終了時に消す)

	// プロジェクト (UI スレッドだけが触る)
	std::optional<Project> m_project;
	RecentProjects         m_recent { RecentProjects::defaultFile() };
	bool                   m_projectBusy = false;

	// 編集の履歴 (UI スレッドだけが触る)
	UndoStack              m_undo;
	bool                   m_editBusy = false;

	// リソースビューアーの最新のピクセルの値 (描画スレッドが書く)
	std::mutex             m_viewerMutex;
	Json                   m_viewerPixel;
	bool                   m_viewerPinned = false;   // スモークテスト
	std::chrono::steady_clock::time_point m_viewerCheckStart;
	uint64_t   m_smokeFrameMark = 0;
	int        m_smokeWindowStep = 0;
	std::chrono::steady_clock::time_point m_smokeWindowStart;

	// 別ウィンドウ (UI スレッドだけが触る)
	struct PreviewEntry
	{
		uint32_t                        id = 0;
		std::unique_ptr<FloatingWindow> window;
		ResourceViewer::Settings        settings;
		std::string                     status;
		bool                            statusKnown = false;   // 描画スレッドから状態が届いた
		bool                            closing     = false;
	};
	std::vector<PreviewEntry>       m_previews;
	uint32_t                        m_nextPreviewId = 1;
	std::unique_ptr<FloatingWindow> m_viewportFrame;   // ビューポートを入れた別ウィンドウ (なければメインのウィンドウの中)
	bool                            m_viewportBusy = false;
	struct { int x = 0, y = 0, w = 0, h = 0; bool visible = false; } m_viewportWebRect;   // Web のパネルの矩形 (戻すときに使う)
	int      m_exitCode      = 0;
};
} // namespace studio
