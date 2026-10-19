#pragma once
// ============================================================
//  editor/src/host/WebHost.h
//
//  webview/webview の包み。UI スレッド (webview_run を呼ぶスレッド) で作り、そのスレッドで動かす。
//    ・トップレベルのウィンドウは webview が作って持つ
//    ・UI の読み込み: 配布時は 1 つにまとめた index.html を、ui フォルダを割り当てた仮想ホスト
//      (https://ui.sunset-studio.example/) で開く。開発時は Vite の開発サーバー (localhost のみ)
//    ・JS → C++: window.__studio_call(method, params) を Bridge に渡す
//    ・C++ → JS: emit(name, payload) で window.__studio_event(name, payload) を呼ぶ
//    ・タイトルバー (enableCustomFrame): OS のタイトルバーを消してクライアント領域を上まで広げ、UI がタイトルバーと
//      メニューを描く (VS Code のような形)。移動 / 上の辺での大きさの変更は、UI のマウスの押下から startDrag /
//      startResize で OS に任せる (画面の端へのスナップ、Win + 矢印もそのまま効く)
//
//  スレッド: post / emit / terminate と Bridge の返答はどのスレッドから呼んでもよい
//  (webview_dispatch で UI スレッドに渡す)。それ以外は UI スレッドから呼ぶこと。
// ============================================================

#include "Bridge.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace studio
{
class WebHost
{
public:
	struct Config
	{
		std::string title    = "SunsetEditor";
		int         width    = 1440;
		int         height   = 900;
		bool        devTools = false;   // 右クリック / F12 で開発者ツールを開ける
	};

	WebHost();
	~WebHost();
	WebHost(const WebHost&)            = delete;
	WebHost& operator=(const WebHost&) = delete;

	// ウィンドウと WebView を作る。失敗したら false (error に理由。WebView2 ランタイムがないなど)
	bool create(const Config& config, std::string& error);

	// UI を読み込む。devUrl が空でなければ開発サーバーを開く (http://localhost / 127.0.0.1 のみ)。
	// 空なら indexHtml を読み込む。どちらもできなければ、案内のページを表示して false を返す
	bool loadUi(const std::filesystem::path& indexHtml, const std::string& devUrl, std::string& error);

	void run();         // ウィンドウが閉じられるか terminate されるまで戻らない
	void terminate();   // どのスレッドからでもよい

	// UI スレッドで fn を実行する (どのスレッドからでもよい。閉じたあとは何もしない)
	void post(std::function<void()> fn);

	// JS にイベントを送る (どのスレッドからでもよい)。フロントエンドが ui.ready を呼ぶまではためておく
	void emit(const std::string& name, const Json& payload);
	// フロントエンドの準備ができた (ui.ready)。ためていたイベントを送る。UI スレッドから呼ぶ
	void markReady();

	// ウィンドウのタイトル (UI スレッドから呼ぶ)
	void setTitle(const std::string& title);

	Bridge& bridge() { return m_bridge; }
	void*   nativeWindow() const;   // Win32 では HWND

	// ---- タイトルバー (UI スレッドから呼ぶ) ----
	// OS のタイトルバーを消す。onMaximized は最大化の状態が変わったときに (UI スレッドから) 呼ばれる
	void enableCustomFrame(std::function<void(bool maximized)> onMaximized);
	bool customFrame() const { return m_customFrame; }
	void minimizeWindow();
	void toggleMaximize();
	bool isMaximized() const;
	void closeWindow();                          // ×ボタンと同じ (閉じる前の後片付けも通る)
	void startDrag();                            // マウスのボタンを押したまま、ウィンドウを動かす
	bool startResize(const std::string& edge);   // "top" / "topleft" / "topright"。知らない辺なら false
	bool openDevTools();                         // 開発者ツール (devTools が有効なときだけ)

	// ウィンドウを閉じる操作 (×ボタンなど) のときに呼ぶ。handler は後片付けを始め、
	// 終わったら proceed を UI スレッドで呼ぶ (呼ぶとウィンドウが閉じる)。UI スレッドから設定すること
	using CloseHandler = std::function<void(std::function<void()> proceed)>;
	void setCloseHandler(CloseHandler handler);

private:
	static intptr_t __stdcall topLevelSubclass(void* hwnd, unsigned msg, uintptr_t wp, intptr_t lp, uintptr_t id, uintptr_t ref);
	void evalNow(const std::string& js);
	void installSubclass();
	static void dispatchTrampoline(void* w, void* arg);

	void*                    m_webview = nullptr;   // webview_t
	Bridge                   m_bridge;
	std::atomic_bool         m_closing { false };
	std::mutex               m_eventMutex;
	bool                     m_ready = false;
	std::vector<std::string> m_pendingEvents;       // ready 前のイベント (JS のコード)
	CloseHandler             m_closeHandler;
	bool                     m_closeApproved = false;
	bool                     m_closePending  = false;
	bool                     m_subclassed    = false;
	bool                     m_customFrame   = false;
	bool                     m_devTools      = false;
	bool                     m_lastMaximized = false;
	std::function<void(bool)> m_onMaximized;
};
} // namespace studio
