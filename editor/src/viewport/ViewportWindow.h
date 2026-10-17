#pragma once
// ============================================================
//  editor/src/viewport/ViewportWindow.h
//
//  3D ビューポートのネイティブの子ウィンドウ。エディターのトップレベルのウィンドウ (webview が作る) の子として、
//  WebView より手前に置く。エンジンはこのウィンドウにスワップチェーンを作って描画する。
//  Web のレイアウトで空けた場所 (data-viewport の要素) の矩形に合わせて動かす (setRect)。
//
//  スレッド: create / destroy / setRect は UI スレッド (ウィンドウを作ったスレッド) から呼ぶ。
//            takeInput / size / visible はどのスレッドからでもよい (描画スレッドが毎フレーム読む)。
//
//  マウス: 左ドラッグ = 回転、中 / 右ドラッグ = 平行移動、ホイール = ズーム、F キー = 視点を戻す
// ============================================================

#include <atomic>
#include <cstdint>
#include <mutex>

namespace studio
{
// 前回の takeInput から積もったカメラの入力 (ピクセル / ホイールの段数)
struct CameraInput
{
	float orbitX = 0, orbitY = 0;   // 左ドラッグ
	float panX   = 0, panY   = 0;   // 中 / 右ドラッグ
	float zoom   = 0;               // ホイール (奥へ回すと正)
	bool  reset  = false;           // F キー

	bool empty() const { return orbitX == 0 && orbitY == 0 && panX == 0 && panY == 0 && zoom == 0 && !reset; }
};

class ViewportWindow
{
public:
	ViewportWindow() = default;
	~ViewportWindow() { destroy(); }
	ViewportWindow(const ViewportWindow&)            = delete;
	ViewportWindow& operator=(const ViewportWindow&) = delete;

	// parent はトップレベルのウィンドウ (Win32 では HWND)。最初は非表示
	bool create(void* parent);
	void destroy();

	// 親のクライアント座標 (物理ピクセル) に置く。幅か高さが 0、または visible = false なら隠す
	void setRect(int x, int y, int width, int height, bool visible);

	void*    nativeHandle() const { return m_hwnd; }
	void*    parentHandle() const { return m_parent; }
	uint32_t width()   const { return m_width.load(); }
	uint32_t height()  const { return m_height.load(); }
	bool     visible() const { return m_visible.load(); }

	// 積もった入力を取り出して空にする (どのスレッドからでもよい)
	CameraInput takeInput();
	// マウスのカーソルの位置 (クライアント座標のピクセル)。ウィンドウの外なら false (どのスレッドからでもよい)
	bool cursor(int& x, int& y) const
	{
		const int64_t v = m_cursor.load();
		if (v < 0) return false;
		x = static_cast<int>(v >> 32);
		y = static_cast<int>(v & 0xFFFFFFFF);
		return true;
	}

	// ウィンドウのメッセージ処理 (OS ごとの実装ファイルのウィンドウプロシージャから呼ぶ)
	intptr_t handleMessage(unsigned msg, uintptr_t wp, intptr_t lp, bool& handled);

private:
	enum class Drag { None, Orbit, Pan };

	void beginDrag(Drag mode, intptr_t lp);

	void*                 m_hwnd   = nullptr;
	void*                 m_parent = nullptr;
	std::atomic<uint32_t> m_width  { 0 };
	std::atomic<uint32_t> m_height { 0 };
	std::atomic_bool      m_visible { false };

	Drag m_drag  = Drag::None;
	int  m_lastX = 0, m_lastY = 0;
	std::atomic<int64_t> m_cursor { -1 };   // (x << 32) | y、外なら -1
	bool                 m_tracking = false; // WM_MOUSELEAVE を頼んだ

	std::mutex  m_inputMutex;
	CameraInput m_input;
};
} // namespace studio
