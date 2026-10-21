#pragma once
// ============================================================
//  editor/src/app/EditorApp.h
//
//  エディター全体の寿命。UI スレッド (main) で WebHost とビューポートのウィンドウを動かし、
//  描画スレッドでエンジンを動かす。JS から呼べるメソッド (Bridge) と、JS に送るイベントをここで登録する。
//
//  メソッド:  ui.ready / app.info / app.ping / viewport.setRect /
//             schema.types / schema.describe /
//             pipeline.list / pipeline.get / pipeline.update / pipeline.growTable / pipeline.save   (パイプラインのアセット)
//             graph.get / graph.set / graph.save                               (ビューポートのレンダーグラフ)
//             project.info / project.recent / project.templates / project.open / project.create / project.close /
//             project.layout.get / project.layout.set / dialog.openProject / dialog.pickFolder   (プロジェクト、M4a)
//             project.migrate (Assets/ の古い構成を、直下に並べる構成に移す、M5)
//             scene.get / scene.set / scene.save / scene.select / scene.pickAt (シーンのファイル、M5-2)
//             edit.undo / edit.redo / edit.state                               (編集の履歴、M4)
//             viewer.get / viewer.set                                          (リソースビューアー、M4)
//             preview.open / preview.set / preview.setRect / preview.close / preview.list
//                                                                              (プレビュー: 別ウィンドウ / タブ、M4)
//             viewport.float / viewport.state                                  (ビューポートを別ウィンドウにする、M4)
//             window.minimize / window.toggleMaximize / window.close / window.startDrag / window.startResize /
//             window.devTools                                                  (UI が描くタイトルバー)
//             asset.list / asset.read / asset.write / asset.templates / asset.create / asset.openExternal /
//             shader.compile / shader.usages / shader.status                   (シェーダー / パイプラインの編集、M4)
//             material.list / material.describe / material.set / material.create / light.types (M5-4 / M5-5)
//             library.list / library.import / library.refresh / library.reimport / library.load
//                                                                              (アセットのライブラリ、M5-3)
//  イベント:  log (エンジンとアプリのメッセージ) / engine.status (エンジンの初期化の結果) /
//             stats (フレーム時間とパスごとの GPU 時間) / graph.changed (グラフが切り替わった) /
//             project.changed (プロジェクトを開いた / 閉じた) / edit.changed (履歴の状態) /
//             pipeline.changed (元に戻す / やり直すでパイプラインが変わった) /
//             viewer.pixel (リソースビューアーのピクセルの値と状態) /
//             preview.changed (プレビューの一覧) / viewport.changed (ビューポートが別ウィンドウか) /
//             shader.status (シェーダーのコンパイルの成否が変わった) /
//             scene.changed (シーンの版と保存の状態) / scene.picked (ビューポートのクリックで選んだ物体) /
//             library.changed (アセットの一覧かモデルの読み込みの状態が変わった) /
//             window.state (最大化の状態が変わった)
// ============================================================

#include "host/WebHost.h"
#include "services/BackgroundQueue.h"
#include "services/Material.h"
#include "services/ProjectAssets.h"
#include "services/Project.h"
#include "services/Scene.h"
#include "services/UndoStack.h"
#include "render/RenderThread.h"
#include "viewport/FloatingWindow.h"
#include "viewport/ViewportWindow.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
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
	bool        nativeTitleBar = false;   // --native-titlebar / STUDIO_NATIVE_TITLEBAR=1: OS のタイトルバーを使う (UI のタイトルバーを出さない)
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
	void registerAssetMethods();    // EditorAssets.cpp
	void registerSceneMethods();    // EditorScene.cpp
	void registerLibraryMethods();  // EditorLibrary.cpp
	// ---- アセットのライブラリ (EditorLibrary.cpp、UI スレッド) ----
	void openLibrary();                               // プロジェクトを開いた / 閉じたとき
	Json libraryJson() const;
	void requestSceneModels();                        // シーンが使うモデルで、まだ読んでいないものを読む
	void requestModel(const std::string& id);         // 別のスレッドで取り込んで読み、描画スレッドに渡す
	void onModelLoaded(uint64_t generation, const std::string& id, uint64_t ticket,
	                   std::shared_ptr<const sunset::asset::Model> model, const sunset::asset::Diagnostics& diag, double ms);
	void requestTexture(const std::string& id);      // マテリアルが使うテクスチャを別のスレッドで読み、描画スレッドに渡す (M5-4)
	// スロットの取り込んだ値 (基本色、金属度、粗さ、テクスチャ) で作る標準のマテリアル
	std::vector<MaterialDesc> slotMaterials(const std::string& modelId, const sunset::asset::Model& model) const;
	void registerMaterialMethods();   // EditorMaterials.cpp
	void ensureMaterialSupport(const Project& project);
	bool writeMaterialFile(const std::string& path, const Json& material, bool create, std::string& error);   // 標準のマテリアルのファイルがなければ、テンプレートから足す
	Json sceneInfoJson() const;
	void emitSceneChanged();
	void pushSceneToRenderer();
	void loadProjectScene();                         // プロジェクトを開いた / 閉じたとき (UI スレッド)
	void applySceneFromUndo(const SceneDesc& scene);
	// シェーダーのコンパイルのメッセージ (描画スレッド)。shader.compile の最中なら集めて true (ログに出さない)
	bool captureShaderMessage(int32_t id, const std::string& text);
	void onShaderStatus(const std::string& enginePath, bool ok, const std::string& text);   // UI スレッド
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
	void runShaderSelfCheck(std::function<void(bool ok, const std::string& error)> done);
	void runSceneSelfCheck(std::function<void(bool ok, const std::string& error)> done);
	void compileSmokeShaders(std::vector<std::string> paths, std::function<void(bool ok, const std::string& error)> done);

	EditorOptions  m_options;
	WebHost        m_host;       // 破棄の順: 描画スレッド → ビューポート → WebView
	ViewportWindow m_viewport;
	RenderThread   m_render;
	std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

	std::mutex               m_engineMutex;
	EngineStatus             m_engineStatus = EngineStatus::Starting;
	RenderThread::InitResult m_engineInfo;

	std::atomic<uint32_t> m_d3dProblems { 0 };   // D3D12 のエラー / 警告の数 (スモークテストで確認する)
	std::atomic<uint32_t> m_viewportErrors { 0 };   // 描画のエラーのログの数 (スモークテストで、最初のフレームまでに 0 か確認する)

	// スモークテストの進み具合 (UI スレッドだけが触る)
	bool     m_uiReady       = false;
	bool     m_infoRequested = false;
	bool     m_engineDone    = false;
	bool     m_engineOk      = false;
	uint64_t m_framesDrawn   = 0;
	enum class SmokeStage { WaitProject, WaitFrames, Inspector, Graph, Viewer, Windows, Shader, WaitAfter, EmptyProject, EmptyFrames, EmptyShaders, Finished };
	SmokeStage m_smokeStage    = SmokeStage::WaitProject;
	bool       m_smokeProjectFailed = false;
	bool       m_smokeSceneReady    = false;   // テンプレートのシーンを既定のシーン (立方体 49 個) に置き換えた
	uint32_t   m_smokeErrorMark     = 0;   // 空のプロジェクトを開く前の、描画のエラーの数
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
	std::map<std::string, std::string> m_shaderErrors;
	// シーン (UI スレッドだけが触る。描画スレッドにはコピーを渡す)
	SceneDesc   m_sceneData;
	std::string m_sceneFile;
	std::string m_sceneError;      // 読めなかった理由 (空でなければ編集 / 保存を断る)
	std::string m_sceneSelection;
	uint64_t    m_sceneRevision = 0;
	bool        m_sceneDirty    = false;   // コンパイルに失敗しているシェーダー (アセットのパス → メッセージ。UI スレッド)
	// アセットのライブラリ (UI スレッドだけが触る。取り込みは m_libraryWorker で)
	ProjectAssets   m_library;
	uint64_t        m_libraryGeneration = 0;   // プロジェクトを開き直したら上げる (前のプロジェクトの結果を捨てる)
	struct ModelState
	{
		std::string status;   // "loading" / "ready" / "error"
		std::string error;
		Json        info;     // ノード / メッシュ / 頂点 / 三角形 / スロットの数
		uint64_t    ticket = 0;
		std::vector<MaterialDesc> slotMaterials;   // スロットごとの取り込んだ値のマテリアル (M5-4)
	};
	std::map<std::string, ModelState> m_modelStates;
	struct TextureState
	{
		std::string status;   // "loading" / "ready" / "error"
		uint64_t    ticket = 0;
	};
	std::map<std::string, TextureState> m_textureStates;
	struct { int x = 0, y = 0, w = 0, h = 0; bool visible = false; } m_viewportWebRect;   // Web のパネルの矩形 (戻すときに使う)
	int      m_exitCode      = 0;
	BackgroundQueue m_libraryWorker;   // 最後に置く (先に止まる)。run の終わりでも止める
};
} // namespace studio
