#pragma once
// ============================================================
//  editor/src/viewport/FloatingWindow.h
//
//  エディターのメインのウィンドウから切り離した、ネイティブのトップレベルのウィンドウ。次の 2 つに使う:
//    ・プレビュー: レンダーグラフのリソースを別ウィンドウで表示する (エンジンがこのウィンドウにスワップチェーンを作る)
//    ・ビューポート: ビューポートの子ウィンドウ (ViewportWindow) をこの中に移して、別ウィンドウにする
//  メインのウィンドウを owner にするので、メインより手前に出て、メインを最小化すると一緒に隠れる。
//
//  ×ボタンではウィンドウを壊さず、onClose を呼ぶだけ (スワップチェーンを解放してから destroy すること)。
//
//  スレッド: create / destroy / setTitle は UI スレッドから呼ぶ。
//            width / height / drawable / nativeHandle はどのスレッドからでもよい (描画スレッドが毎フレーム読む)。
// ============================================================

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace studio
{
class FloatingWindow
{
public:
	struct Callbacks
	{
		std::function<void()>                   close;    // ×ボタン (UI スレッド)
		std::function<void(uint32_t, uint32_t)> resize;   // クライアント領域の大きさが変わった (UI スレッド)
	};

	FloatingWindow() = default;
	~FloatingWindow() { destroy(); }
	FloatingWindow(const FloatingWindow&)            = delete;
	FloatingWindow& operator=(const FloatingWindow&) = delete;

	// owner はメインのウィンドウ (Win32 では HWND)。x / y はスクリーン座標 (物理ピクセル)、x < 0 なら OS に任せる。
	// width / height はクライアント領域の大きさ。作ったら表示する
	bool create(void* owner, const std::string& titleUtf8, int x, int y, int width, int height, Callbacks callbacks);
	void destroy();

	void setTitle(const std::string& titleUtf8);
	void focus();

	void*    nativeHandle() const { return m_hwnd.load(); }
	uint32_t width()  const { return m_width.load(); }
	uint32_t height() const { return m_height.load(); }
	// 描いてよい (表示されていて、最小化されておらず、大きさがある)
	bool     drawable() const;

	// ウィンドウのメッセージ処理 (OS ごとの実装ファイルのウィンドウプロシージャから呼ぶ)
	intptr_t handleMessage(unsigned msg, uintptr_t wp, intptr_t lp, bool& handled);

private:
	std::atomic<void*>    m_hwnd { nullptr };
	std::atomic<uint32_t> m_width  { 0 };
	std::atomic<uint32_t> m_height { 0 };
	Callbacks             m_callbacks;
};
} // namespace studio
