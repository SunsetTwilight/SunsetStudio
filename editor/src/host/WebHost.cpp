// ============================================================
//  editor/src/host/WebHost.cpp
// ============================================================
#include "WebHost.h"

// 宣言だけを読み込む (実装は静的ライブラリ webview::core_static)
#define WEBVIEW_HEADER
#include "webview/webview.h"

#include <windows.h>
#include <commctrl.h>
#include <ole2.h>   // WebView2.h が使う COM の宣言 (WIN32_LEAN_AND_MEAN では入らない)
#include <WebView2.h>
#include <wrl/client.h>

#include <fstream>
#include <iterator>
#include <sstream>

namespace studio
{
namespace
{
webview_t wv(void* p) { return static_cast<webview_t>(p); }

// ページより先に実行する。フロントエンドが受け取り口を差し替えるまでのイベントをためる
const char* k_InitScript = R"js(
(function () {
	if (window.__studio_event) return;
	var pending = [];
	window.__studio_event = function (name, payload) { pending.push([name, payload]); };
	window.__studio_take_pending = function () { return pending.splice(0, pending.length); };
})();
)js";

// 配布用の UI を配る仮想ホスト (WebView2 の SetVirtualHostNameToFolderMapping)。
// 実在しないドメイン (.example) にして、ネットワークの名前と重ならないようにする
const wchar_t* k_UiHostW = L"ui.sunset-studio.example";
const char*    k_UiUrl   = "https://ui.sunset-studio.example/index.html";

// webview が作った WebView2 に、UI のフォルダを仮想ホストとして割り当てる
bool mapUiFolder(webview_t w, const std::filesystem::path& folder, std::string& error)
{
	using Microsoft::WRL::ComPtr;
	auto* controller = static_cast<ICoreWebView2Controller*>(webview_get_native_handle(w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
	ComPtr<ICoreWebView2> core;
	ComPtr<ICoreWebView2_3> core3;
	if (!controller || FAILED(controller->get_CoreWebView2(&core)) || !core || FAILED(core.As(&core3)))
		return error = "WebView2 の仮想ホストを使えません (WebView2 ランタイムが古い可能性があります)", false;
	if (FAILED(core3->SetVirtualHostNameToFolderMapping(k_UiHostW, folder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS)))
		return error = "UI のフォルダを仮想ホストに割り当てられません: " + folder.string(), false;
	return true;
}

bool isLocalDevUrl(const std::string& url)
{
	for (const char* prefix : { "http://localhost:", "http://127.0.0.1:", "http://localhost/", "http://127.0.0.1/" })
		if (url.rfind(prefix, 0) == 0)
			return true;
	return false;
}

std::string htmlEscape(const std::string& s)
{
	std::string out;
	for (char c : s)
	{
		switch (c)
		{
		case '&': out += "&amp;";  break;
		case '<': out += "&lt;";   break;
		case '>': out += "&gt;";   break;
		case '"': out += "&quot;"; break;
		default:  out += c;
		}
	}
	return out;
}

std::string fallbackPage(const std::string& message)
{
	return R"(<!doctype html><html lang="ja"><head><meta charset="utf-8"><title>SunsetEditor</title>
<style>body{font:14px system-ui,sans-serif;background:#1e1f22;color:#dfe1e5;margin:40px}code{background:#2b2d30;padding:2px 6px;border-radius:4px}</style>
</head><body><h2>UI を読み込めませんでした</h2><p>)" + htmlEscape(message) + R"(</p>
<p>UI をビルドするには CMake のオプション <code>STUDIO_BUILD_UI=ON</code> (既定) で構成し直すか、
<code>editor/frontend</code> で <code>npm run dev</code> を実行して、環境変数 <code>STUDIO_UI_DEV_URL=http://localhost:5173</code> を設定してください。</p>
</body></html>)";
}
} // namespace

WebHost::WebHost()
	: m_bridge([this](const std::string& id, int status, const std::string& json) {
		// 返答は UI スレッドで webview_return する (別のスレッドからの返答もここを通る)
		post([this, id, status, json] {
			webview_return(wv(m_webview), id.c_str(), status, json.c_str());
		});
	})
{
}

WebHost::~WebHost()
{
	m_closing = true;
	if (m_webview)
		webview_destroy(wv(m_webview));
}

bool WebHost::create(const Config& config, std::string& error)
{
	webview_t w = webview_create(config.devTools ? 1 : 0, nullptr);
	if (!w)
	{
		error = "WebView を作成できませんでした。Microsoft Edge WebView2 ランタイムがインストールされているか確認してください "
		        "(https://developer.microsoft.com/microsoft-edge/webview2/)";
		return false;
	}
	m_webview  = w;
	m_devTools = config.devTools;
	webview_set_title(w, config.title.c_str());
	webview_set_size(w, config.width, config.height, WEBVIEW_HINT_NONE);
	webview_init(w, k_InitScript);
	webview_bind(w, "__studio_call",
		[](const char* id, const char* req, void* arg) {
			static_cast<WebHost*>(arg)->m_bridge.handleCall(id ? id : "", req ? req : "[]");
		},
		this);
	return true;
}

bool WebHost::loadUi(const std::filesystem::path& indexHtml, const std::string& devUrl, std::string& error)
{
	if (!devUrl.empty())
	{
		if (!isLocalDevUrl(devUrl))
		{
			error = "STUDIO_UI_DEV_URL は http://localhost か http://127.0.0.1 の URL だけを指定できます: " + devUrl;
			webview_set_html(wv(m_webview), fallbackPage(error).c_str());
			return false;
		}
		webview_navigate(wv(m_webview), devUrl.c_str());
		return true;
	}

	std::error_code ec;
	if (!std::filesystem::is_regular_file(indexHtml, ec))
	{
		error = "UI のファイルが見つかりません: " + indexHtml.string();
		webview_set_html(wv(m_webview), fallbackPage(error).c_str());
		return false;
	}
	// UI のフォルダを https://ui.sunset-studio.example/ として配り、index.html を開く。
	// (NavigateToString = webview_set_html は 2 MB までなので、Monaco を含む UI は渡せない)
	std::string mapError;
	if (mapUiFolder(wv(m_webview), indexHtml.parent_path(), mapError))
	{
		webview_navigate(wv(m_webview), k_UiUrl);
		return true;
	}
	// 仮想ホストを使えない古いランタイム: 2 MB 以下なら文字列で渡す
	std::ifstream in(indexHtml, std::ios::binary);
	const std::string html((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (html.size() < 2u * 1024u * 1024u)
	{
		webview_set_html(wv(m_webview), html.c_str());
		return true;
	}
	error = mapError + " (WebView2 ランタイムを更新してください)";
	webview_set_html(wv(m_webview), fallbackPage(error).c_str());
	return false;
}

void WebHost::run()
{
	webview_run(wv(m_webview));
	m_closing = true;   // ループを抜けたら、以降の post は捨てる
}

void WebHost::terminate()
{
	if (m_webview && !m_closing)
		webview_terminate(wv(m_webview));
}

void WebHost::dispatchTrampoline(void* /*w*/, void* arg)
{
	auto* fn = static_cast<std::function<void()>*>(arg);
	(*fn)();
	delete fn;
}

void WebHost::post(std::function<void()> fn)
{
	if (!m_webview || m_closing)
		return;
	auto* heap = new std::function<void()>(std::move(fn));
	if (webview_dispatch(wv(m_webview),
			[](webview_t w, void* arg) { dispatchTrampoline(w, arg); }, heap) != WEBVIEW_ERROR_OK)
		delete heap;
}

void WebHost::evalNow(const std::string& js)
{
	webview_eval(wv(m_webview), js.c_str());
}

void WebHost::emit(const std::string& name, const Json& payload)
{
	const std::string js = "window.__studio_event && window.__studio_event("
		+ Json(name).dump() + "," + payload.dump(-1, ' ', false, Json::error_handler_t::replace) + ");";
	{
		std::lock_guard lock(m_eventMutex);
		if (!m_ready)
		{
			if (m_pendingEvents.size() < 10000)   // 準備ができないまま溜まり続けないように
				m_pendingEvents.push_back(js);
			return;
		}
	}
	post([this, js] { evalNow(js); });
}

void WebHost::markReady()
{
	std::vector<std::string> pending;
	{
		std::lock_guard lock(m_eventMutex);
		m_ready = true;
		pending.swap(m_pendingEvents);
	}
	if (pending.empty())
		return;
	std::string js;
	for (const auto& e : pending)
		js += e + "\n";
	// ui.ready の返答より後に届くように、UI スレッドのキューに積む
	post([this, js] { evalNow(js); });
}

void WebHost::setTitle(const std::string& title)
{
	if (m_webview)
		webview_set_title(wv(m_webview), title.c_str());
}

void* WebHost::nativeWindow() const
{
	return m_webview ? webview_get_window(wv(m_webview)) : nullptr;
}

void WebHost::setCloseHandler(CloseHandler handler)
{
	m_closeHandler = std::move(handler);
	installSubclass();
}

void WebHost::installSubclass()
{
	HWND top = static_cast<HWND>(nativeWindow());
	if (top && !m_subclassed)
		m_subclassed = SetWindowSubclass(top,
			reinterpret_cast<SUBCLASSPROC>(&WebHost::topLevelSubclass), 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
}

void WebHost::enableCustomFrame(std::function<void(bool maximized)> onMaximized)
{
	HWND top = static_cast<HWND>(nativeWindow());
	if (!top)
		return;
	m_onMaximized = std::move(onMaximized);
	m_customFrame = true;
	installSubclass();
	m_lastMaximized = IsZoomed(top) != FALSE;
	// 枠を計算し直させる (WM_NCCALCSIZE でタイトルバーの分をクライアント領域にする)
	SetWindowPos(top, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WebHost::minimizeWindow()
{
	if (HWND top = static_cast<HWND>(nativeWindow()))
		ShowWindow(top, SW_MINIMIZE);
}

void WebHost::toggleMaximize()
{
	if (HWND top = static_cast<HWND>(nativeWindow()))
		ShowWindow(top, IsZoomed(top) ? SW_RESTORE : SW_MAXIMIZE);
}

bool WebHost::isMaximized() const
{
	HWND top = static_cast<HWND>(nativeWindow());
	return top && IsZoomed(top);
}

void WebHost::cancelClose()
{
	m_closePending = false;
}

void WebHost::closeWindow()
{
	if (HWND top = static_cast<HWND>(nativeWindow()))
		PostMessageW(top, WM_CLOSE, 0, 0);
}

void WebHost::startDrag()
{
	HWND top = static_cast<HWND>(nativeWindow());
	if (!top)
		return;
	// WebView が持っているマウスのキャプチャを外し、タイトルバーを押したことにして OS の移動に任せる
	POINT pt {};
	GetCursorPos(&pt);
	ReleaseCapture();
	PostMessageW(top, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
}

bool WebHost::startResize(const std::string& edge)
{
	HWND top = static_cast<HWND>(nativeWindow());
	if (!top || IsZoomed(top))
		return false;
	WPARAM hit = 0;
	if (edge == "top")           hit = HTTOP;
	else if (edge == "topleft")  hit = HTTOPLEFT;
	else if (edge == "topright") hit = HTTOPRIGHT;
	else return false;
	POINT pt {};
	GetCursorPos(&pt);
	ReleaseCapture();
	PostMessageW(top, WM_NCLBUTTONDOWN, hit, MAKELPARAM(pt.x, pt.y));
	return true;
}

bool WebHost::openDevTools()
{
	if (!m_webview || !m_devTools)
		return false;
	auto* controller = static_cast<ICoreWebView2Controller*>(
		webview_get_native_handle(wv(m_webview), WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
	if (!controller)
		return false;
	Microsoft::WRL::ComPtr<ICoreWebView2> core;
	if (FAILED(controller->get_CoreWebView2(&core)) || !core)
		return false;
	return SUCCEEDED(core->OpenDevToolsWindow());
}

intptr_t __stdcall WebHost::topLevelSubclass(void* hwnd, unsigned msg, uintptr_t wp, intptr_t lp, uintptr_t /*id*/, uintptr_t ref)
{
	auto* self = reinterpret_cast<WebHost*>(ref);
	HWND  h    = static_cast<HWND>(hwnd);
	if (msg == WM_CLOSE && self->m_closeHandler && !self->m_closeApproved)
	{
		// 後片付け (描画スレッドのスワップチェーンの解放など) が終わってから閉じる
		if (!self->m_closePending)
		{
			self->m_closePending = true;
			self->m_closeHandler([self, h] {
				self->m_closeApproved = true;
				PostMessageW(h, WM_CLOSE, 0, 0);
			});
		}
		return 0;
	}
	if (self->m_customFrame && msg == WM_NCCALCSIZE && wp)
	{
		// 左右と下の枠 (大きさを変える縁) は OS のまま、上のタイトルバーの分だけクライアント領域にする
		auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
		const LONG top = p->rgrc[0].top;
		const LRESULT r = DefSubclassProc(h, msg, wp, lp);
		p->rgrc[0].top = top;
		// 最大化すると、ウィンドウは枠の分だけ画面の外にはみ出すので、その分を下げる
		if (IsZoomed(h))
		{
			const UINT dpi = GetDpiForWindow(h);
			p->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
		}
		return r;
	}
	if (self->m_customFrame && msg == WM_SIZE)
	{
		const bool maximized = wp == SIZE_MAXIMIZED;
		if (wp != SIZE_MINIMIZED && maximized != self->m_lastMaximized)
		{
			self->m_lastMaximized = maximized;
			if (self->m_onMaximized)
				self->m_onMaximized(maximized);
		}
	}
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(h, reinterpret_cast<SUBCLASSPROC>(&WebHost::topLevelSubclass), 1);
	return DefSubclassProc(h, msg, wp, lp);
}
} // namespace studio
