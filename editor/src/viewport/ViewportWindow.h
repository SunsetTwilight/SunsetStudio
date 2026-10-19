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
//  マウス: 左ドラッグ = 回転、中 / 右ドラッグ = 平行移動、ホイール = ズーム、F キー = 視点を戻す、
//          左クリック (ほとんど動かさずに離す) = 物体を選ぶ
// ============================================================

#include <atomic>
#include <vector>
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

	// 親を替える (メインのウィンドウ ⇔ 別ウィンドウ (FloatingWindow))。スワップチェーンを解放してから呼ぶこと
	void reparent(void* parent);

	// 親のクライアント座標 (物理ピクセル) に置く。幅か高さが 0、または visible = false なら隠す
	void setRect(int x, int y, int width, int height, bool visible);

	// 穴 (このウィンドウのクライアント座標の物理ピクセル)。Web のメニューなどがビューポートに重なる部分を切り抜き、
	// その下の WebView を見せる (子ウィンドウは Web の UI より手前に出るため)。空なら切り抜かない
	struct Hole { int x = 0, y = 0, width = 0, height = 0; };
	void setHoles(const std::vector<Hole>& holes);

	void*    nativeHandle() const { return m_hwnd; }
	void*    parentHandle() const { return m_parent.load(); }
	uint32_t width()   const { return m_width.load(); }
	uint32_t height()  const { return m_height.load(); }
	bool     visible() const { return m_visible.load(); }

	// 積もった入力を取り出して空にする (どのスレッドからでもよい)
	CameraInput takeInput();
	// 最後の左クリックの位置を取り出す (どのスレッドからでもよい)。なければ false
	bool takeClick(int& x, int& y)
	{
		const int64_t v = m_click.exchange(-1);
		if (v < 0) return false;
		x = static_cast<int>(v >> 32);
		y = static_cast<int>(v & 0xFFFFFFFF);
		return true;
	}

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
	std::atomic<void*>    m_parent { nullptr };
	std::atomic<uint32_t> m_width  { 0 };
	std::atomic<uint32_t> m_height { 0 };
	std::atomic_bool      m_visible { false };
	std::vector<Hole> m_holes;   // 最後に設定した穴 (UI スレッド)

	Drag m_drag  = Drag::None;
	int  m_lastX = 0, m_lastY = 0;
	std::atomic<int64_t> m_cursor { -1 };   // (x << 32) | y、外なら -1
	bool                 m_tracking = false; // WM_MOUSELEAVE を頼んだ
	int                  m_downX = 0, m_downY = 0;   // 左ボタンを押した位置 (クリックの判定)
	int                  m_moved = 0;                // 押してから動いた距離 (ピクセル、マンハッタン)
	std::atomic<int64_t> m_click { -1 };             // (x << 32) | y、なければ -1

	std::mutex  m_inputMutex;
	CameraInput m_input;
};
} // namespace studio
