// ============================================================
//  editor/src/viewport/FloatingWindow_win32.cpp
// ============================================================
#include "FloatingWindow.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>

namespace studio
{
namespace
{
const wchar_t* k_ClassName = L"SunsetEditorFloating";

HWND hwndOf(void* p) { return static_cast<HWND>(p); }

std::wstring widen(const std::string& s)
{
	if (s.empty())
		return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
	return w;
}

LRESULT CALLBACK floatingWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_NCCREATE)
	{
		auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
	}
	if (auto* self = reinterpret_cast<FloatingWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
	{
		bool handled = false;
		const intptr_t r = self->handleMessage(msg, wp, lp, handled);
		if (handled)
			return r;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

bool registerClass()
{
	static bool registered = false;
	if (registered)
		return true;
	WNDCLASSEXW wc   = {};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = floatingWndProc;
	wc.hInstance     = GetModuleHandleW(nullptr);
	wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW
	wc.hbrBackground = nullptr;   // 背景はエンジンが描く
	wc.lpszClassName = k_ClassName;
	// アイコンはメインのウィンドウ (exe) と同じもの
	wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
	registered       = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	return registered;
}
} // namespace

bool FloatingWindow::create(void* owner, const std::string& titleUtf8, int x, int y, int width, int height, Callbacks callbacks)
{
	if (m_hwnd.load() || !registerClass())
		return false;
	m_callbacks = std::move(callbacks);
	const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
	RECT r = { 0, 0, width, height };
	AdjustWindowRectEx(&r, style, FALSE, 0);
	const int outerW = r.right - r.left, outerH = r.bottom - r.top;
	int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
	if (x >= 0)
	{
		// 置きたい場所 (カーソルの位置など) がタイトルバーの中ほどに来るようにし、画面の外に出さない
		px = x - outerW / 2;
		py = y - (-r.top) / 2;
		const HMONITOR mon = MonitorFromPoint(POINT{ x, y }, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfoW(mon, &mi))
		{
			px = std::max<int>(mi.rcWork.left, std::min<int>(px, mi.rcWork.right - outerW));
			py = std::max<int>(mi.rcWork.top, std::min<int>(py, mi.rcWork.bottom - outerH));
		}
	}
	const std::wstring title = widen(titleUtf8);
	HWND h = CreateWindowExW(0, k_ClassName, title.c_str(), style, px, py, outerW, outerH,
		hwndOf(owner), nullptr, GetModuleHandleW(nullptr), this);
	if (!h)
		return false;
	m_hwnd = h;
	RECT cr;
	GetClientRect(h, &cr);
	m_width  = static_cast<uint32_t>(cr.right - cr.left);
	m_height = static_cast<uint32_t>(cr.bottom - cr.top);
	ShowWindow(h, SW_SHOW);
	return true;
}

void FloatingWindow::destroy()
{
	if (void* h = m_hwnd.exchange(nullptr))
	{
		SetWindowLongPtrW(hwndOf(h), GWLP_USERDATA, 0);
		DestroyWindow(hwndOf(h));
	}
	m_width = m_height = 0;
}

void FloatingWindow::setTitle(const std::string& titleUtf8)
{
	if (void* h = m_hwnd.load())
		SetWindowTextW(hwndOf(h), widen(titleUtf8).c_str());
}

void FloatingWindow::focus()
{
	if (void* h = m_hwnd.load())
	{
		if (IsIconic(hwndOf(h)))
			ShowWindow(hwndOf(h), SW_RESTORE);
		SetForegroundWindow(hwndOf(h));
	}
}

bool FloatingWindow::drawable() const
{
	void* h = m_hwnd.load();
	return h && IsWindowVisible(hwndOf(h)) && !IsIconic(hwndOf(h)) && m_width.load() > 0 && m_height.load() > 0;
}

intptr_t FloatingWindow::handleMessage(unsigned msg, uintptr_t, intptr_t lp, bool& handled)
{
	handled = true;
	switch (msg)
	{
	case WM_ERASEBKGND:
		return 1;   // 消去しない (エンジンが描く)
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(hwndOf(m_hwnd.load()), &ps);
		EndPaint(hwndOf(m_hwnd.load()), &ps);
		return 0;
	}
	case WM_SIZE:
	{
		const uint32_t w = LOWORD(lp), h = HIWORD(lp);
		if (w > 0 && h > 0)   // 最小化 (0 x 0) では大きさを変えない
		{
			m_width  = w;
			m_height = h;
		}
		if (m_callbacks.resize)
			m_callbacks.resize(w, h);
		return 0;
	}
	case WM_CLOSE:
		if (m_callbacks.close)
			m_callbacks.close();   // 後片付けのあとで destroy される
		return 0;
	default:
		break;
	}
	handled = false;
	return 0;
}
} // namespace studio
