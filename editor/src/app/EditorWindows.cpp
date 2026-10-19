// ============================================================
//  editor/src/app/EditorWindows.cpp
//
//  メインのウィンドウから切り離す、ネイティブの別ウィンドウ (M4)。
//    プレビュー     preview.open / preview.set / preview.setRect / preview.close / preview.list、イベント preview.changed
//                   グラフのリソース 1 つを、それ専用のウィンドウに表示する (ノードエディターの右クリックなど)。
//                   mode = "window" はトップレベルの別ウィンドウ、"panel" はメインのウィンドウの子 (タブのパネルの
//                   矩形に preview.setRect で合わせる。ビューポートと同じ方式)
//    ビューポート   viewport.float / viewport.state、イベント viewport.changed
//                   ビューポートの子ウィンドウを別ウィンドウ (FloatingWindow) に移す / 戻す
//  ウィンドウは UI スレッドで作って壊す。スワップチェーンは描画スレッドが持つので、
//  壊す前に描画スレッドで解放してもらう (RenderThread::removePreview / resetViewport の done のあとで壊す)。
// ============================================================
#include "EditorApp.h"

#include <algorithm>
#include <cmath>

namespace studio
{
namespace
{
std::string previewTitle(const ResourceViewer::Settings& s, const std::string& status)
{
	std::string t = s.resource + " - プレビュー - SunsetEditor";
	if (!status.empty())
		t = s.resource + " (表示できません: " + status + ") - プレビュー - SunsetEditor";
	return t;
}

// JS のスクリーン座標 (CSS px) を物理ピクセルにする。なければ -1
void screenPoint(const Json& p, int& x, int& y)
{
	x = y = -1;
	if (!p.contains("x") || !p.contains("y") || !p.at("x").is_number() || !p.at("y").is_number())
		return;
	const double dpr = p.value("dpr", 1.0);
	x = static_cast<int>(std::lround(p.at("x").get<double>() * dpr));
	y = static_cast<int>(std::lround(p.at("y").get<double>() * dpr));
}
} // namespace

Json EditorApp::previewsJson() const
{
	Json list = Json::array();
	for (const auto& e : m_previews)
		if (!e.closing)
			list.push_back({ { "id", e.id }, { "resource", e.settings.resource }, { "status", e.status },
			                 { "mode", e.window->isChild() ? "panel" : "window" } });
	return { { "previews", list } };
}

void EditorApp::emitPreviews()
{
	m_host.emit("preview.changed", previewsJson());
}

void EditorApp::closePreview(uint32_t id)
{
	const auto it = std::find_if(m_previews.begin(), m_previews.end(), [id](const PreviewEntry& e) { return e.id == id; });
	if (it == m_previews.end() || it->closing)
		return;
	it->closing = true;
	emitPreviews();
	m_render.removePreview(id, [this, id] {
		m_host.post([this, id] {
			std::erase_if(m_previews, [id](const PreviewEntry& e) { return e.id == id; });   // ウィンドウを壊す
			checkSmokeTest();
		});
	});
}

void EditorApp::closeAllPreviews()
{
	std::vector<uint32_t> ids;
	for (const auto& e : m_previews)
		ids.push_back(e.id);
	for (uint32_t id : ids)
		closePreview(id);
}

void EditorApp::onPreviewStatus(uint32_t id, const std::string& status)
{
	const auto it = std::find_if(m_previews.begin(), m_previews.end(), [id](const PreviewEntry& e) { return e.id == id; });
	if (it == m_previews.end() || it->closing)
		return;
	const bool changed = !it->statusKnown || it->status != status;
	it->status      = status;
	it->statusKnown = true;
	if (changed)
	{
		if (!it->window->isChild())
			it->window->setTitle(previewTitle(it->settings, status));
		emitPreviews();
	}
	checkSmokeTest();
}

void EditorApp::floatViewport(bool floating, int x, int y, std::function<void(bool, const std::string&)> done)
{
	if (m_viewportBusy)
		return done(false, "ビューポートを移しています");
	if (floating == static_cast<bool>(m_viewportFrame))
	{
		if (m_viewportFrame)
			m_viewportFrame->focus();
		return done(true, {});
	}
	m_viewportBusy = true;
	const uint32_t lastW = m_viewport.width(), lastH = m_viewport.height();
	// 先に隠す (描画スレッドが古い親のまま、スワップチェーンを作り直さないように)
	m_viewport.setRect(0, 0, 0, 0, false);
	m_render.resetViewport([this, floating, x, y, lastW, lastH, done] {
		m_host.post([this, floating, x, y, lastW, lastH, done] {
			m_viewportBusy = false;
			if (floating)
			{
				auto frame = std::make_unique<FloatingWindow>();
				FloatingWindow::Callbacks cb;
				cb.resize = [this](uint32_t w, uint32_t h) {
					if (!m_viewportFrame)
						return;
					m_viewport.setRect(0, 0, static_cast<int>(w), static_cast<int>(h), w > 0 && h > 0);
					m_render.wake();
				};
				cb.close = [this] { floatViewport(false, -1, -1, [](bool, const std::string&) {}); };
				const int w = lastW >= 64 ? static_cast<int>(lastW) : 960;
				const int h = lastH >= 64 ? static_cast<int>(lastH) : 600;
				if (!frame->create(m_host.nativeWindow(), "ビューポート - SunsetEditor", x, y, w, h, std::move(cb)))
				{
					m_host.emit("viewport.changed", { { "floating", false } });
					return done(false, "ウィンドウを作れません");
				}
				m_viewport.reparent(frame->nativeHandle());
				m_viewportFrame = std::move(frame);
				m_viewport.setRect(0, 0, static_cast<int>(m_viewportFrame->width()), static_cast<int>(m_viewportFrame->height()), true);
				log("info", "viewport", "ビューポートを別ウィンドウにしました");
			}
			else
			{
				m_viewport.reparent(m_host.nativeWindow());
				m_viewportFrame.reset();
				// 最後に受け取った Web のパネルの矩形に戻す (パネルも viewport.changed を受けて送り直す)
				const auto& rc = m_viewportWebRect;
				m_viewport.setRect(rc.x, rc.y, rc.w, rc.h, rc.visible);
				log("info", "viewport", "ビューポートをメインのウィンドウに戻しました");
			}
			m_render.wake();
			m_host.emit("viewport.changed", { { "floating", floating } });
			done(true, {});
		});
	});
}

void EditorApp::releaseWindows(std::function<void()> done)
{
	closeAllPreviews();
	if (!m_viewportFrame)
	{
		// プレビューの後片付け (描画スレッドの順番どおり) のあとで done を呼ぶ
		m_render.resetViewport([this, done] { m_host.post(done); });
		return;
	}
	m_viewport.setRect(0, 0, 0, 0, false);
	m_render.resetViewport([this, done] {
		m_host.post([this, done] {
			m_viewport.reparent(m_host.nativeWindow());
			m_viewportFrame.reset();
			done();
		});
	});
}

void EditorApp::registerWindowMethods()
{
	Bridge& b = m_host.bridge();

	b.registerMethod("preview.list", [this](const Json&, Bridge::Reply r) { r.ok(previewsJson()); });

	// ---- UI が描くタイトルバー (メインのウィンドウの操作) ----
	b.registerMethod("window.minimize", [this](const Json&, Bridge::Reply r) {
		m_host.minimizeWindow();
		r.ok({ { "maximized", m_host.isMaximized() } });
	});
	b.registerMethod("window.toggleMaximize", [this](const Json&, Bridge::Reply r) {
		m_host.toggleMaximize();
		r.ok({ { "maximized", m_host.isMaximized() } });
	});
	b.registerMethod("window.close", [this](const Json&, Bridge::Reply r) {
		r.ok(Json::object());
		m_host.closeWindow();   // ×ボタンと同じ (後片付けをしてから閉じる)
	});
	// マウスのボタンを押したところから、ウィンドウの移動 / 上の辺での大きさの変更を OS に任せる
	b.registerMethod("window.startDrag", [this](const Json&, Bridge::Reply r) {
		m_host.startDrag();
		r.ok(Json::object());
	});
	b.registerMethod("window.startResize", [this](const Json& p, Bridge::Reply r) {
		if (!m_host.startResize(p.value("edge", std::string())))
			return r.error(BridgeError::InvalidParams, "edge は top / topleft / topright です (最大化しているときは使えません)");
		r.ok(Json::object());
	});
	b.registerMethod("window.devTools", [this](const Json&, Bridge::Reply r) {
		if (!m_host.openDevTools())
			return r.error(BridgeError::Unavailable, "開発者ツールは使えません (--devtools か STUDIO_DEVTOOLS=1 で起動してください)");
		r.ok(Json::object());
	});

	// リソースを別ウィンドウに表示する。同じリソースのウィンドウがあれば、設定を変えて前に出す
	b.registerMethod("preview.open", [this](const Json& p, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		ResourceViewer::Settings s;
		s.enabled = true;
		std::string error;
		if (!applyViewerPatch(p, s, error))
			return r.error(BridgeError::InvalidParams, error);
		if (s.resource.empty())
			return r.error(BridgeError::InvalidParams, "resource がありません");
		s.enabled = true;
		s.pinX = s.pinY = -1;
		const std::string mode = p.value("mode", "window");
		if (mode != "window" && mode != "panel")
			return r.error(BridgeError::InvalidParams, "mode は window か panel です");
		const bool panel = mode == "panel";
		// 別ウィンドウは同じリソースなら 1 つにする (タブはパネルごとに別)
		for (auto& e : m_previews)
			if (!panel && !e.closing && !e.window->isChild() && e.settings.resource == s.resource)
			{
				e.settings = s;
				m_render.setPreview(e.id, s);
				e.window->focus();
				return r.ok({ { "id", e.id } });
			}

		int x = -1, y = -1;
		screenPoint(p, x, y);
		// 大きさはビューポートの縦横比で、幅 640 (ビューポートがなければ 16:10)
		const uint32_t vw = m_viewport.width(), vh = m_viewport.height();
		const int w = 640;
		const int h = (vw > 0 && vh > 0) ? std::clamp(static_cast<int>(640.0 * vh / vw), 200, 900) : 400;

		const uint32_t id = m_nextPreviewId++;
		PreviewEntry entry;
		entry.id       = id;
		entry.settings = s;
		entry.window   = std::make_unique<FloatingWindow>();
		FloatingWindow::Callbacks cb;
		cb.close  = [this, id] { closePreview(id); };
		cb.resize = [this](uint32_t, uint32_t) { m_render.wake(); };
		const bool created = panel ? entry.window->createChild(m_host.nativeWindow(), std::move(cb))
		                           : entry.window->create(m_host.nativeWindow(), previewTitle(s, {}), x, y, w, h, std::move(cb));
		if (!created)
			return r.error(BridgeError::Internal, "プレビューのウィンドウを作れません");
		m_render.addPreview(id, entry.window.get(), s);
		m_previews.push_back(std::move(entry));
		m_render.wake();
		emitPreviews();
		r.ok({ { "id", id } });
	});

	// プレビューの設定を変える (書かれた項目だけ)
	b.registerMethod("preview.set", [this](const Json& p, Bridge::Reply r) {
		const uint32_t id = p.value("id", 0u);
		const auto it = std::find_if(m_previews.begin(), m_previews.end(), [id](const PreviewEntry& e) { return e.id == id && !e.closing; });
		if (it == m_previews.end())
			return r.error(BridgeError::InvalidParams, "そのプレビューはありません");
		ResourceViewer::Settings s = it->settings;
		std::string error;
		if (!applyViewerPatch(p, s, error))
			return r.error(BridgeError::InvalidParams, error);
		s.enabled = true;
		it->settings = s;
		it->window->setTitle(previewTitle(s, it->status));
		m_render.setPreview(id, s);
		emitPreviews();
		r.ok(viewerJson(s));
	});

	// タブのプレビュー: Web のパネルの矩形 (WebView のクライアント座標の CSS px) と dpr。viewport.setRect と同じ形
	b.registerMethod("preview.setRect", [this](const Json& p, Bridge::Reply r) {
		const uint32_t id = p.value("id", 0u);
		const auto it = std::find_if(m_previews.begin(), m_previews.end(), [id](const PreviewEntry& e) { return e.id == id && !e.closing; });
		if (it == m_previews.end() || !it->window->isChild())
			return r.error(BridgeError::InvalidParams, "そのタブのプレビューはありません");
		try
		{
			const double dpr = p.value("dpr", 1.0);
			const double fx = p.at("x").get<double>(), fy = p.at("y").get<double>();
			const int x  = static_cast<int>(std::lround(fx * dpr));
			const int y  = static_cast<int>(std::lround(fy * dpr));
			const int x2 = static_cast<int>(std::lround((fx + p.at("width").get<double>()) * dpr));
			const int y2 = static_cast<int>(std::lround((fy + p.at("height").get<double>()) * dpr));
			it->window->setRect(x, y, std::max(0, x2 - x), std::max(0, y2 - y), p.value("visible", true));
		}
		catch (const std::exception& e)
		{
			return r.error(BridgeError::InvalidParams, std::string("値が不正です: ") + e.what());
		}
		m_render.wake();
		r.ok(Json::object());
	});

	b.registerMethod("preview.close", [this](const Json& p, Bridge::Reply r) {
		const uint32_t id = p.value("id", 0u);
		closePreview(id);
		r.ok(previewsJson());
	});

	b.registerMethod("viewport.state", [this](const Json&, Bridge::Reply r) {
		r.ok({ { "floating", static_cast<bool>(m_viewportFrame) } });
	});

	// ビューポートを別ウィンドウにする / 戻す。x / y (CSS px のスクリーン座標) と dpr があれば、そこに出す
	b.registerMethod("viewport.float", [this](const Json& p, Bridge::Reply r) {
		const bool floating = p.value("floating", true);
		int x = -1, y = -1;
		screenPoint(p, x, y);
		floatViewport(floating, x, y, [this, r](bool ok, const std::string& error) {
			if (!ok)
				return r.error(BridgeError::Unavailable, error);
			r.ok({ { "floating", static_cast<bool>(m_viewportFrame) } });
		});
	});
}
} // namespace studio
