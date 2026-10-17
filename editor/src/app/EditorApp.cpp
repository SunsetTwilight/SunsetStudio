// ============================================================
//  editor/src/app/EditorApp.cpp
// ============================================================
#include "EditorApp.h"
#include "services/SchemaJson.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

#ifndef STUDIO_VERSION
#define STUDIO_VERSION "0.0.0"
#endif

namespace studio
{
namespace
{
// JS とやり取りする形式のバージョン。互換性のない変更をしたら上げる
constexpr int k_ProtocolVersion = 1;

std::string getEnv(const char* name)
{
	char*  value = nullptr;
	size_t len   = 0;
	if (_dupenv_s(&value, &len, name) != 0 || !value)
		return {};
	std::string s(value);
	free(value);
	return s;
}

const char* severityName(GfxMessageSeverity s)
{
	switch (s)
	{
	case GfxMessageSeverity::Corruption: return "error";
	case GfxMessageSeverity::Error:      return "error";
	case GfxMessageSeverity::Warning:    return "warning";
	default:                             return "info";
	}
}

// メッセージの id から出どころを決める (負の id はエンジン自身のメッセージ)
const char* sourceName(int32_t id)
{
	if (id == GFX_MESSAGE_ID_SHADER_COMPILE) return "shader";
	if (id == GFX_MESSAGE_ID_PIPELINE)       return "pipeline";
	if (id < 0)                              return "engine";
	return "d3d12";
}

const char* apiName(GraphicsAPI api)
{
	switch (api)
	{
	case GraphicsAPI::DirectX12: return "DirectX 12";
	case GraphicsAPI::Vulkan:    return "Vulkan";
	default:                     return "";
	}
}
} // namespace

EditorOptions EditorOptions::fromEnvironment(int argc, char** argv)
{
	EditorOptions o;
	o.uiDevUrl = getEnv("STUDIO_UI_DEV_URL");
#ifndef NDEBUG
	o.devTools = true;
#endif
	if (getEnv("STUDIO_DEVTOOLS") == "1")
		o.devTools = true;
	if (getEnv("STUDIO_ADAPTER") == "software")
		o.software = true;
	for (int i = 1; i < argc; ++i)
	{
		const std::string_view a = argv[i];
		if (a == "--software")   o.software  = true;
		if (a == "--smoke-test") o.smokeTest = true;
		if (a == "--devtools")   o.devTools  = true;
	}
	return o;
}

int EditorApp::run()
{
	// ---- ウィンドウと WebView ----
	WebHost::Config hostConfig;
	hostConfig.devTools = m_options.devTools;
	std::string error;
	if (!m_host.create(hostConfig, error))
	{
		std::cerr << "[SunsetEditor] " << error << "\n";
		return 1;
	}
	if (!m_viewport.create(m_host.nativeWindow()))
	{
		std::cerr << "[SunsetEditor] ビューポートのウィンドウを作れませんでした\n";
		return 1;
	}
	registerMethods();

	// ---- エンジン (描画スレッド) ----
	RenderThread::Config renderConfig;
	renderConfig.device.appName = "SunsetEditor";
	renderConfig.device.adapter = m_options.software ? GfxAdapterType::Software : GfxAdapterType::Hardware;
#ifdef STUDIO_GFX_VALIDATION
	renderConfig.device.validation = true;
#endif
	renderConfig.shaderDirs = m_options.shaderDirs;

	RenderThread::Callbacks callbacks;
	callbacks.message = [this](GfxMessageSeverity s, int32_t id, const std::string& text) { onEngineMessage(s, id, text); };
	callbacks.init    = [this](const RenderThread::InitResult& r) { onEngineInit(r); };
	callbacks.stats   = [this](const RenderThread::FrameStats& st) { onStats(st); };
	callbacks.log     = [this](const std::string& level, const std::string& text) { log(level.c_str(), "viewport", text); };
	m_render.start(renderConfig, std::move(callbacks));
	m_render.attachViewport(&m_viewport);

	// ×ボタンで閉じるときは、ビューポートのスワップチェーンを先に解放してからウィンドウを閉じる
	m_host.setCloseHandler([this](std::function<void()> proceed) {
		m_render.detachViewport([this, proceed] { m_host.post(proceed); });
	});

	// ---- UI ----
	if (!m_host.loadUi(m_options.uiIndex, m_options.uiDevUrl, error))
	{
		log("error", "app", error);
		if (m_options.smokeTest)
		{
			m_render.stop();
			return 3;
		}
	}
	else
		log("info", "app", m_options.uiDevUrl.empty() ? "UI: " + m_options.uiIndex.string() : "UI (開発サーバー): " + m_options.uiDevUrl);

	// ---- スモークテストの時間切れ ----
	std::mutex              watchdogMutex;
	std::condition_variable watchdogCv;
	bool                    finished = false;
	std::thread watchdog;
	if (m_options.smokeTest)
	{
		watchdog = std::thread([&] {
			std::unique_lock lock(watchdogMutex);
			if (!watchdogCv.wait_for(lock, std::chrono::seconds(m_options.smokeTimeoutSec), [&] { return finished; }))
				m_host.post([this] {
					std::cerr << "[SunsetEditor] smoke test: 時間切れ (ui.ready=" << m_uiReady << ", app.info=" << m_infoRequested
					          << ", engine=" << m_engineDone << ", frames=" << m_framesDrawn << ")\n";
					m_exitCode = 2;
					m_host.terminate();
				});
		});
	}

	m_host.run();

	if (watchdog.joinable())
	{
		{
			std::lock_guard lock(watchdogMutex);
			finished = true;
		}
		watchdogCv.notify_one();
		watchdog.join();
	}
	// エンジン (とスワップチェーン) を終了してから、ビューポートのウィンドウと WebView を破棄する
	m_render.stop();
	m_viewport.destroy();
	return m_exitCode;
}

void EditorApp::registerMethods()
{
	Bridge& b = m_host.bridge();

	// フロントエンドの準備ができた。ためていたイベントを送る
	b.registerMethod("ui.ready", [this](const Json&, Bridge::Reply r) {
		r.ok({ { "protocol", k_ProtocolVersion } });
		m_host.markReady();
		m_uiReady = true;
		checkSmokeTest();
	});

	// アプリとエンジンの情報
	b.registerMethod("app.info", [this](const Json&, Bridge::Reply r) {
		Json engine;
		{
			std::lock_guard lock(m_engineMutex);
			engine = engineJson();
		}
		r.ok({
			{ "app",      "SunsetEditor" },
			{ "version",  STUDIO_VERSION },
			{ "protocol", k_ProtocolVersion },
			{ "engine",   engine },
			{ "methods",  m_host.bridge().methods() },
		});
		m_infoRequested = true;
		checkSmokeTest();
	});

	// ビューポートの矩形 (WebView のクライアント座標の CSS px) と devicePixelRatio。
	// 物理ピクセルに直して子ウィンドウを動かす。描画スレッドはその大きさに合わせてスワップチェーンを作り直す
	b.registerMethod("viewport.setRect", [this](const Json& p, Bridge::Reply r) {
		const double dpr     = p.value("dpr", 1.0);
		const bool   visible = p.value("visible", true);
		const int x  = static_cast<int>(std::lround(p.at("x").get<double>() * dpr));
		const int y  = static_cast<int>(std::lround(p.at("y").get<double>() * dpr));
		const int x2 = static_cast<int>(std::lround((p.at("x").get<double>() + p.at("width").get<double>()) * dpr));
		const int y2 = static_cast<int>(std::lround((p.at("y").get<double>() + p.at("height").get<double>()) * dpr));
		m_viewport.setRect(x, y, std::max(0, x2 - x), std::max(0, y2 - y), visible);
		m_render.wake();
		r.ok({ { "width", m_viewport.width() }, { "height", m_viewport.height() }, { "visible", m_viewport.visible() } });
	});

	// 疎通の確認。params をそのまま返す
	b.registerMethod("app.ping", [this](const Json& p, Bridge::Reply r) {
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_startTime).count();
		r.ok({ { "echo", p }, { "uptimeMs", ms } });
	});

	// ---- スキーマ (インスペクターのフォームを作るための型の説明) ----
	b.registerMethod("schema.types", [](const Json&, Bridge::Reply r) {
		r.ok(listTypes());
	});
	b.registerMethod("schema.describe", [](const Json& p, Bridge::Reply r) {
		const std::string type = p.at("type").get<std::string>();
		Json d = describeType(type);
		if (d.is_null())
			r.error(BridgeError::InvalidParams, "型 '" + type + "' はスキーマにありません");
		else
			r.ok(std::move(d));
	});

	// ---- パイプライン (描画スレッドが持つので、描画スレッドで処理して返答する) ----
	b.registerMethod("pipeline.list", [this](const Json&, Bridge::Reply r) {
		m_render.postScene([r](GraphicsEngine&, SceneRenderer* scene) {
			if (!scene)
				return r.error(BridgeError::Unavailable, "ビューポートのシーンがありません (エンジンの初期化に失敗した可能性があります)");
			Json list = Json::array();
			for (const auto& p : scene->pipelines())
				list.push_back({ { "id", p.id }, { "label", p.label }, { "type", "GfxGraphicsPipelineDesc" } });
			r.ok(list);
		});
	});

	b.registerMethod("pipeline.get", [this](const Json& p, Bridge::Reply r) {
		const std::string id = p.at("id").get<std::string>();
		m_render.postScene([r, id](GraphicsEngine& engine, SceneRenderer* scene) {
			const auto* entry = scene ? scene->findPipeline(id) : nullptr;
			GfxGraphicsPipelineDesc desc;
			if (!entry || !engine.getPipelineDesc(entry->handle, desc))
				return r.error(BridgeError::InvalidParams, "パイプライン '" + id + "' はありません");
			r.ok({ { "id", id }, { "type", "GfxGraphicsPipelineDesc" },
			       { "desc", structToJson(gfxStructInfo<GfxGraphicsPipelineDesc>(), &desc) } });
		});
	});

	// desc は変えるフィールドだけでもよい (書かれていないフィールドは今の値のまま)。
	// 値が不正ならエラー (フィールドのパス付き)、PSO を作れなければエラー (理由はログ) で、どちらも元のまま
	b.registerMethod("pipeline.update", [this](const Json& p, Bridge::Reply r) {
		const std::string id   = p.at("id").get<std::string>();
		const Json        json = p.at("desc");
		m_render.postScene([this, r, id, json](GraphicsEngine& engine, SceneRenderer* scene) {
			const auto* entry = scene ? scene->findPipeline(id) : nullptr;
			GfxGraphicsPipelineDesc desc;
			if (!entry || !engine.getPipelineDesc(entry->handle, desc))
				return r.error(BridgeError::InvalidParams, "パイプライン '" + id + "' はありません");

			const ApplyResult applied = applyJson(json, desc);
			for (const auto& w : applied.warnings)
				log("warning", "inspector", id + ": " + w);
			if (!applied.ok)
			{
				std::string message = "値が不正です";
				for (const auto& e : applied.errors)
				{
					message += "\n" + e;
					log("error", "inspector", id + ": " + e);
				}
				return r.error(BridgeError::InvalidParams, message);
			}
			desc.debugName = nullptr;   // 名前は今のまま
			if (!engine.updatePipeline(entry->handle, desc))
				return r.error(BridgeError::Internal, "パイプラインを作り直せませんでした (理由はログを確認してください)。元の設定のままです");

			GfxGraphicsPipelineDesc current;
			engine.getPipelineDesc(entry->handle, current);
			r.ok({ { "id", id }, { "desc", structToJson(gfxStructInfo<GfxGraphicsPipelineDesc>(), &current) },
			       { "warnings", applied.warnings } });
		});
	});
}

Json EditorApp::engineJson()
{
	const char* status = m_engineStatus == EngineStatus::Ready  ? "ready"
	                   : m_engineStatus == EngineStatus::Failed ? "failed"
	                   :                                          "starting";
	Json j = { { "status", status }, { "abi", GFX_ABI_VERSION } };
	if (m_engineStatus == EngineStatus::Ready)
	{
		j["adapter"] = m_engineInfo.adapter;
		j["api"]     = apiName(m_engineInfo.api);
	}
	if (m_engineStatus == EngineStatus::Failed)
		j["error"] = m_engineInfo.error;
	return j;
}

void EditorApp::onEngineInit(const RenderThread::InitResult& result)
{
	// 描画スレッドから呼ばれる
	Json status;
	{
		std::lock_guard lock(m_engineMutex);
		m_engineInfo   = result;
		m_engineStatus = result.ok ? EngineStatus::Ready : EngineStatus::Failed;
		status         = engineJson();
	}
	if (result.ok)
		log("info", "engine", "エンジンを初期化しました: " + result.adapter + " / " + apiName(result.api));
	else
		log("error", "engine", "エンジンを初期化できませんでした: " + result.error);
	m_host.emit("engine.status", status);

	const bool ok = result.ok;
	m_host.post([this, ok] {
		m_engineDone = true;
		m_engineOk   = ok;
		checkSmokeTest();
	});
}

void EditorApp::onEngineMessage(GfxMessageSeverity severity, int32_t id, const std::string& text)
{
	// D3D12 のデバッグレイヤーのエラー / 警告 (id >= 0)。スモークテストでは 0 件であることを確認する
	if (id >= 0 && severity <= GfxMessageSeverity::Warning)
		++m_d3dProblems;
	log(severityName(severity), sourceName(id), text, id);
}

void EditorApp::onStats(const RenderThread::FrameStats& s)
{
	// 描画スレッドから呼ばれる
	m_host.emit("stats", {
		{ "fps",     s.fps },
		{ "frameMs", s.frameMs },
		{ "cpuMs",   s.cpuMs },
		{ "width",   s.width },
		{ "height",  s.height },
		{ "frames",  s.frames },
	});
	const uint64_t frames = s.frames;
	m_host.post([this, frames] {
		m_framesDrawn = frames;
		checkSmokeTest();
	});
}

void EditorApp::log(const char* level, const char* source, const std::string& text, int32_t id)
{
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_startTime).count();
	std::cerr << "[" << source << " " << level << "] " << text << "\n";
	m_host.emit("log", { { "timeMs", ms }, { "level", level }, { "source", source }, { "id", id }, { "text", text } });
}

void EditorApp::checkSmokeTest()
{
	if (!m_options.smokeTest || m_smokeStage == SmokeStage::Finished || !m_uiReady || !m_infoRequested || !m_engineDone)
		return;

	auto finish = [this](int code, const std::string& what) {
		m_smokeStage = SmokeStage::Finished;
		const uint32_t problems = m_d3dProblems.load();
		if (code == 0 && problems > 0)
			code = 5;
		m_exitCode = code;
		std::cerr << "[SunsetEditor] smoke test: " << (code == 0 ? "OK" : what.empty() ? "D3D12 のエラー / 警告がある" : what)
		          << " (frames=" << m_framesDrawn << ", d3d12=" << problems << ")\n";
		// すでに UI スレッドのキューに積んだイベントを送ってから閉じる
		m_host.post([this] { m_host.terminate(); });
	};

	if (!m_engineOk)
		return finish(4, "エンジンの初期化に失敗");

	switch (m_smokeStage)
	{
	case SmokeStage::WaitFrames:
		// ビューポートが描けるまで待つ
		if (m_framesDrawn < m_options.smokeFrames)
			return;
		m_smokeStage = SmokeStage::Inspector;
		runInspectorSelfCheck([this, finish](bool ok, const std::string& error) {
			// 返答は描画スレッドから届くので、UI スレッドで続ける
			m_host.post([this, finish, ok, error] {
				if (!ok)
					return finish(6, "インスペクターの経路の確認に失敗: " + error);
				m_smokeStage    = SmokeStage::WaitAfter;
				m_smokeFrameMark = m_framesDrawn;
				checkSmokeTest();
			});
		});
		return;
	case SmokeStage::WaitAfter:
		// パイプラインを作り直したあとも描けていること
		if (m_framesDrawn < m_smokeFrameMark + 20)
			return;
		return finish(0, "");
	default:
		return;
	}
}

// pipeline.get / pipeline.update を、フロントエンドと同じ経路 (Bridge のメソッド) で呼ぶ
//   1. 正しい変更 (カリングなし + ワイヤーフレーム) が反映される
//   2. 不正な値はエラーになる
//   3. 元に戻す
void EditorApp::runInspectorSelfCheck(std::function<void(bool ok, const std::string& error)> done)
{
	Bridge& b = m_host.bridge();
	const Json change  = { { "rasterizerState", { { "cullMode", "None" }, { "fillMode", "Wireframe" } } } };
	const Json invalid = { { "rasterizerState", { { "cullMode", "Sideways" } } } };
	const Json restore = { { "rasterizerState", { { "cullMode", "Back" }, { "fillMode", "Solid" } } } };

	b.callLocal("pipeline.update", { { "id", "scene" }, { "desc", change } }, [this, done, invalid, restore](int status, const Json& r) {
		if (status != 0 || r["desc"]["rasterizerState"]["fillMode"] != "Wireframe")
			return done(false, "変更が反映されない: " + r.dump());
		log("info", "smoke", "pipeline.update: ワイヤーフレームに変更");
		m_host.bridge().callLocal("pipeline.update", { { "id", "scene" }, { "desc", invalid } }, [this, done, restore](int status2, const Json& r2) {
			if (status2 == 0 || r2.value("code", 0) != static_cast<int>(BridgeError::InvalidParams))
				return done(false, "不正な値がエラーにならない: " + r2.dump());
			m_host.bridge().callLocal("pipeline.update", { { "id", "scene" }, { "desc", restore } }, [done](int status3, const Json& r3) {
				if (status3 != 0 || r3["desc"]["rasterizerState"]["cullMode"] != "Back")
					return done(false, "元に戻せない: " + r3.dump());
				done(true, {});
			});
		});
	});
}
} // namespace studio
