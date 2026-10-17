// ============================================================
//  editor/src/viewport/ViewportWindow_win32.cpp
// ============================================================
#include "ViewportWindow.h"

#include <windows.h>
#include <windowsx.h>

namespace studio
{
namespace
{
const wchar_t* k_ClassName = L"SunsetEditorViewport";

HWND hwndOf(void* p) { return static_cast<HWND>(p); }

LRESULT CALLBACK viewportWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_NCCREATE)
	{
		auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
	}
	if (auto* self = reinterpret_cast<ViewportWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
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
	wc.lpfnWndProc   = viewportWndProc;
	wc.hInstance     = GetModuleHandleW(nullptr);
	wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW (UNICODE を定義していないので W 版の値を直接使う)
	wc.hbrBackground = nullptr;   // 背景はエンジンが描く (ちらつかないように消去しない)
	wc.lpszClassName = k_ClassName;
	registered       = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	return registered;
}
} // namespace

bool ViewportWindow::create(void* parent)
{
	if (m_hwnd || !parent || !registerClass())
		return false;
	m_parent = parent;
	// WS_CLIPSIBLINGS: 兄弟 (WebView) と重なった部分を描かせない
	HWND h = CreateWindowExW(0, k_ClassName, L"Viewport",
		WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		0, 0, 1, 1, hwndOf(parent), nullptr, GetModuleHandleW(nullptr), this);
	m_hwnd = h;
	return h != nullptr;
}

void ViewportWindow::destroy()
{
	if (m_hwnd)
	{
		SetWindowLongPtrW(hwndOf(m_hwnd), GWLP_USERDATA, 0);
		DestroyWindow(hwndOf(m_hwnd));
		m_hwnd = nullptr;
	}
	m_visible = false;
}

void ViewportWindow::setRect(int x, int y, int width, int height, bool visible)
{
	if (!m_hwnd)
		return;
	const bool show = visible && width > 0 && height > 0;
	if (show)
	{
		// WebView より手前に置く (WebView が並びを変えることがあるので毎回指定する)
		SetWindowPos(hwndOf(m_hwnd), HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		m_width  = static_cast<uint32_t>(width);
		m_height = static_cast<uint32_t>(height);
	}
	else
		ShowWindow(hwndOf(m_hwnd), SW_HIDE);
	m_visible = show;
}

CameraInput ViewportWindow::takeInput()
{
	std::lock_guard lock(m_inputMutex);
	CameraInput out = m_input;
	m_input = {};
	return out;
}

void ViewportWindow::beginDrag(Drag mode, intptr_t lp)
{
	m_drag  = mode;
	m_lastX = GET_X_LPARAM(lp);
	m_lastY = GET_Y_LPARAM(lp);
	SetCapture(hwndOf(m_hwnd));
	SetFocus(hwndOf(m_hwnd));   // ホイールとキーをビューポートで受け取る
}

intptr_t ViewportWindow::handleMessage(unsigned msg, uintptr_t wp, intptr_t lp, bool& handled)
{
	handled = true;
	switch (msg)
	{
	case WM_ERASEBKGND:
		return 1;   // 消去しない (エンジンが描く)
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(hwndOf(m_hwnd), &ps);   // 描画は描画スレッドの Present。ここでは無効領域を消すだけ
		EndPaint(hwndOf(m_hwnd), &ps);
		return 0;
	}
	case WM_MOUSEACTIVATE:
		return MA_ACTIVATE;
	case WM_LBUTTONDOWN:
		beginDrag(Drag::Orbit, lp);
		return 0;
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		beginDrag(Drag::Pan, lp);
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		if (m_drag != Drag::None)
		{
			m_drag = Drag::None;
			ReleaseCapture();
		}
		return 0;
	case WM_CAPTURECHANGED:
		m_drag = Drag::None;
		return 0;
	case WM_MOUSEMOVE:
	{
		if (m_drag == Drag::None)
			return 0;
		const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
		const float dx = static_cast<float>(x - m_lastX), dy = static_cast<float>(y - m_lastY);
		m_lastX = x;
		m_lastY = y;
		std::lock_guard lock(m_inputMutex);
		if (m_drag == Drag::Orbit) { m_input.orbitX += dx; m_input.orbitY += dy; }
		else                       { m_input.panX   += dx; m_input.panY   += dy; }
		return 0;
	}
	case WM_MOUSEWHEEL:
	{
		std::lock_guard lock(m_inputMutex);
		m_input.zoom += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
		return 0;
	}
	case WM_KEYDOWN:
		if (wp == 'F')
		{
			std::lock_guard lock(m_inputMutex);
			m_input.reset = true;
			return 0;
		}
		break;
	default:
		break;
	}
	handled = false;
	return 0;
}
} // namespace studio
