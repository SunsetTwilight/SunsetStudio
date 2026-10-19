// ============================================================
//  editor/src/viewport/ViewportWindow_win32.cpp
// ============================================================
#include "ViewportWindow.h"

#include <algorithm>

#include <windows.h>
#include <windowsx.h>

#include <cstdlib>

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

void ViewportWindow::reparent(void* parent)
{
	if (!m_hwnd || !parent || parent == m_parent.load())
		return;
	ShowWindow(hwndOf(m_hwnd), SW_HIDE);
	m_visible = false;
	SetParent(hwndOf(m_hwnd), hwndOf(parent));
	m_parent = parent;
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

void ViewportWindow::setHoles(const std::vector<Hole>& holes)
{
	if (!m_hwnd)
		return;
	auto same = [](const Hole& a, const Hole& b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; };
	if (holes.size() == m_holes.size() && std::equal(holes.begin(), holes.end(), m_holes.begin(), same))
		return;
	m_holes = holes;
	if (holes.empty())
	{
		SetWindowRgn(hwndOf(m_hwnd), nullptr, TRUE);
		return;
	}
	HRGN rgn = CreateRectRgn(0, 0, 0x7FFF, 0x7FFF);
	for (const Hole& h : holes)
	{
		HRGN hole = CreateRectRgn(h.x, h.y, h.x + h.width, h.y + h.height);
		CombineRgn(rgn, rgn, hole, RGN_DIFF);
		DeleteObject(hole);
	}
	SetWindowRgn(hwndOf(m_hwnd), rgn, TRUE);   // 領域はウィンドウが持つ (消さない)
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
		m_downX = GET_X_LPARAM(lp);
		m_downY = GET_Y_LPARAM(lp);
		m_moved = 0;
		return 0;
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		beginDrag(Drag::Pan, lp);
		return 0;
	case WM_LBUTTONUP:
		// ほとんど動かさずに離したらクリック (物体を選ぶ)
		if (m_drag == Drag::Orbit && m_moved <= 4)
		{
			const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
			if (x >= 0 && y >= 0)
				m_click = (static_cast<int64_t>(x) << 32) | static_cast<int64_t>(y);
		}
		[[fallthrough]];
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
	case WM_MOUSELEAVE:
		m_tracking = false;
		if (m_drag == Drag::None)
			m_cursor = -1;
		return 0;
	case WM_MOUSEMOVE:
	{
		const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
		// カーソルの位置 (リソースビューアーのピクセルの値に使う)
		if (x >= 0 && y >= 0)
			m_cursor = (static_cast<int64_t>(x) << 32) | static_cast<int64_t>(y);
		if (!m_tracking)
		{
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwndOf(m_hwnd), 0 };
			m_tracking = TrackMouseEvent(&tme) != FALSE;
		}
		if (m_drag == Drag::None)
			return 0;
		const float dx = static_cast<float>(x - m_lastX), dy = static_cast<float>(y - m_lastY);
		m_moved += std::abs(x - m_lastX) + std::abs(y - m_lastY);
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
