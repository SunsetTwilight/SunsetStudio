#pragma once
// ============================================================
//  editor/src/host/WebHost.h
//
//  webview/webview の包み。UI スレッド (webview_run を呼ぶスレッド) で作り、そのスレッドで動かす。
//    ・トップレベルのウィンドウは webview が作って持つ
//    ・UI の読み込み: 配布時は 1 つにまとめた index.html、開発時は Vite の開発サーバー (localhost のみ)
//    ・JS → C++: window.__studio_call(method, params) を Bridge に渡す
//    ・C++ → JS: emit(name, payload) で window.__studio_event(name, payload) を呼ぶ
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

	Bridge& bridge() { return m_bridge; }
	void*   nativeWindow() const;   // Win32 では HWND

private:
	void evalNow(const std::string& js);
	static void dispatchTrampoline(void* w, void* arg);

	void*                    m_webview = nullptr;   // webview_t
	Bridge                   m_bridge;
	std::atomic_bool         m_closing { false };
	std::mutex               m_eventMutex;
	bool                     m_ready = false;
	std::vector<std::string> m_pendingEvents;       // ready 前のイベント (JS のコード)
};
} // namespace studio
