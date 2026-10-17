// ============================================================
//  editor/src/app/EditorApp.cpp
// ============================================================
#include "EditorApp.h"

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
	registerMethods();

	// ---- エンジン (描画スレッド) ----
	DeviceConfig cfg;
	cfg.appName = "SunsetEditor";
	cfg.adapter = m_options.software ? GfxAdapterType::Software : GfxAdapterType::Hardware;
#ifdef STUDIO_GFX_VALIDATION
	cfg.validation = true;
#endif
	m_render.start(cfg,
		[this](GfxMessageSeverity s, int32_t id, const std::string& text) { onEngineMessage(s, id, text); },
		[this](const RenderThread::InitResult& r) { onEngineInit(r); });

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
					          << ", engine=" << m_engineDone << ")\n";
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
	// エンジンを終了してから WebView を破棄する (描画スレッドからの返答が WebView を使わないように)
	m_render.stop();
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

	// 疎通の確認。params をそのまま返す
	b.registerMethod("app.ping", [this](const Json& p, Bridge::Reply r) {
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_startTime).count();
		r.ok({ { "echo", p }, { "uptimeMs", ms } });
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
	log(severityName(severity), sourceName(id), text, id);
}

void EditorApp::log(const char* level, const char* source, const std::string& text, int32_t id)
{
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_startTime).count();
	std::cerr << "[" << source << " " << level << "] " << text << "\n";
	m_host.emit("log", { { "timeMs", ms }, { "level", level }, { "source", source }, { "id", id }, { "text", text } });
}

void EditorApp::checkSmokeTest()
{
	if (!m_options.smokeTest || m_smokeFinished || !m_uiReady || !m_infoRequested || !m_engineDone)
		return;
	m_smokeFinished = true;
	std::cerr << "[SunsetEditor] smoke test: " << (m_engineOk ? "OK" : "エンジンの初期化に失敗") << "\n";
	m_exitCode = m_engineOk ? 0 : 4;
	// すでに UI スレッドのキューに積んだイベントを送ってから閉じる
	m_host.post([this] { m_host.terminate(); });
}
} // namespace studio
