#pragma once
// ============================================================
//  editor/src/render/ResourceViewer.h
//
//  リソースビューアー (M4)。レンダーグラフの途中のテクスチャ (G-Buffer、深度など) を、
//  ビューポートに最終画像の代わりに表示する。
//    ・チャンネル (R / G / B / A の組み合わせ。1 つだけなら白黒)、mip、深度の線形化、露出、表示する範囲、ガンマ
//    ・カーソルの下のピクセルの値を、Compute で読んでリードバック (ABI 8) で CPU に戻す (数フレーム遅れる)
//  表示はグラフの記録のあとに、同じコマンドバッファでバックバッファに上書きする。
//  プレビューのウィンドウ (別ウィンドウで表示) は draw だけを使い、設定と SRV をウィンドウごとに持つ。
//  対象は 2D (配列でない、MSAA でない、整数でない) のテクスチャ。それ以外は理由を返して表示しない。
//
//  描画スレッドだけが使う。
// ============================================================

#include "gfx/core/GraphicsEngine.h"
#include "gfx/rendergraph/Executor.h"

#include <array>
#include <string>

namespace studio
{
class ResourceViewer
{
public:
	struct Settings
	{
		bool        enabled  = false;
		std::string resource;            // グラフのリソースの名前
		uint32_t    mip      = 0;
		uint32_t    channels = 0x7;       // bit0 = R, bit1 = G, bit2 = B, bit3 = A
		bool        depthLinear = false;  // 深度を視点からの距離にする (近い = 黒)
		float       exposure = 0.0f;      // EV (2^exposure 倍)
		float       rangeMin = 0.0f;      // 表示する範囲 (深度の線形化のときは距離)
		float       rangeMax = 1.0f;
		bool        gamma    = true;      // 表示のときにガンマ (1/2.2) をかける
		int         pinX = -1, pinY = -1; // 0 以上なら、カーソルの代わりにこのテクセル (表示している mip の座標) を読む
	};

	// 最新のピクセルの値
	struct Probe
	{
		bool                 valid = false;
		std::string          resource;
		uint32_t             x = 0, y = 0, mip = 0;
		std::array<float, 4> value {};
		bool                 depth    = false;   // 深度のフォーマット
		float                distance = 0.0f;    // 深度のとき、視点からの距離 (value[0] から求める)
	};

	// 表示している mip の SRV (テクスチャか mip が変わったら作り直す)。表示先ごとに持つ
	struct ViewCache
	{
		ViewHandle srv;
		uint64_t   texture = 0;
		uint32_t   mip     = 0;
	};
	// 表示した矩形 (ピクセル) と、テクスチャの 1 テクセルあたりのピクセル数
	struct DrawInfo
	{
		float    x = 0, y = 0, scale = 1;
		uint32_t texWidth = 0, texHeight = 0, mip = 0;
		bool     depth = false;
	};

	bool init(GraphicsEngine& engine, std::string& error);
	void shutdown(GraphicsEngine& engine);

	Settings&       settings()       { return m_settings; }
	const Settings& settings() const { return m_settings; }

	// グラフの記録のあとに呼ぶ。rt (w x h) に表示し、カーソル (なければ cursorX < 0) のピクセルを読む。
	// 表示できなければ status に理由を入れて何もしない
	void record(GraphicsEngine& engine, ICommandBuffer* cb, const gfx::rg::Executor& executor,
	            IRenderTarget* rt, uint32_t w, uint32_t h, int cursorX, int cursorY, float zNear, float zFar);

	// rt (w x h) に settings のリソースを表示するだけ (settings.enabled は見ない)。表示できなければ false と status に理由
	bool draw(GraphicsEngine& engine, ICommandBuffer* cb, const gfx::rg::Executor& executor, const Settings& settings,
	          ViewCache& cache, IRenderTarget* rt, uint32_t w, uint32_t h, float zNear, float zFar,
	          std::string& status, DrawInfo* info = nullptr);
	static void releaseCache(GraphicsEngine& engine, ViewCache& cache);

	const std::string& status() const { return m_status; }   // 空なら表示できている
	// 読み終わった最新のピクセルの値 (前回から新しいものがなければ valid = false)
	Probe takeProbe(GraphicsEngine& engine);

private:
	struct Slot
	{
		BufferHandle readback;
		bool         pending = false;
		uint64_t     frame   = 0;
		Probe        probe;
	};

	Settings            m_settings;
	std::string         m_status;
	BindingLayoutHandle m_viewLayout, m_probeLayout;
	PipelineHandle      m_viewPipe, m_probePipe;
	ShaderHandle        m_vs, m_ps, m_cs;
	BufferHandle        m_probeBuffer;   // Compute が書く (Storage)
	ViewHandle          m_probeUav;
	ViewCache           m_cache;         // ビューポートに表示しているテクスチャの SRV
	std::array<Slot, 3> m_slots;
	uint64_t            m_frame = 0, m_lastProbeFrame = 0;
	float               m_zNear = 0.1f, m_zFar = 1000.0f;
};
} // namespace studio
