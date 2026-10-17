// ============================================================
//  editor/src/app/EditorApp.cpp
// ============================================================
#include "EditorApp.h"
#include "services/SchemaJson.h"

#include "gfx/rendergraph/GraphJson.h"
#include "host/FileDialog.h"

#include <fstream>
#include <process.h>   // _getpid
#include <sstream>

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
#ifndef STUDIO_GFX_VERSION
#define STUDIO_GFX_VERSION ""
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

const char* skipReasonName(gfx::rg::SkippedPass::Reason r)
{
	switch (r)
	{
	case gfx::rg::SkippedPass::Reason::Disabled:  return "disabled";
	case gfx::rg::SkippedPass::Reason::Condition: return "condition";
	case gfx::rg::SkippedPass::Reason::Culled:    return "culled";
	}
	return "";
}

// UI に送るグラフの情報 (描画スレッドで作る)
Json graphJson(ViewportRenderer& scene)
{
	gfx::rg::GraphAsset& g = scene.graph();
	const gfx::rg::CompiledGraph& c = g.compiled();
	Json order = Json::array();
	for (const auto& p : c.passes)
		order.push_back(p.name);
	Json skipped = Json::array();
	for (const auto& sp : c.skipped)
		skipped.push_back({ { "name", sp.name }, { "reason", skipReasonName(sp.reason) } });
	Json diagnostics = Json::array();
	for (const auto& d : c.diagnostics)
		diagnostics.push_back({ { "level", d.severity == gfx::rg::Diagnostic::Severity::Error ? "error" : "warning" },
		                        { "path", d.path }, { "text", d.message } });
	Json resources = Json::array();
	for (const auto& r : c.resources)
		resources.push_back({ { "name", r.name }, { "kind", gfx::rg::nameOf(r.kind) }, { "used", r.used() },
		                      { "firstPass", r.firstPass }, { "lastPass", r.lastPass } });
	const std::u8string file = (std::filesystem::path("graphs") / g.file().filename()).generic_u8string();
	return {
		{ "file",        std::string(file.begin(), file.end()) },
		{ "revision",    g.revision() },
		{ "graph",       Json::parse(gfx::rg::graphToString(g.desc())) },
		{ "order",       order },
		{ "skipped",     skipped },
		{ "diagnostics", diagnostics },
		{ "resources",   resources },
		{ "pipelines",   scene.pipelineFiles() },
	};
}

// 編集の結果 (描画スレッドで作る)
struct EditOutcome
{
	bool                     ok   = false;
	BridgeError              code = BridgeError::InvalidParams;
	std::string              message;
	Json                     result;
	Json                     before, after;   // 履歴に入れる内容
	std::vector<std::string> warnings;
};

// グラフ全体の JSON に切り替える
EditOutcome applyGraphEdit(ViewportRenderer& scene, const std::string& text)
{
	EditOutcome o;
	gfx::rg::GraphDesc desc;
	GfxJsonDiagnostics diag;
	if (!gfx::rg::graphFromString(text, desc, &diag))
	{
		o.message = "グラフの JSON が不正です";
		for (const auto& e : diag.errors) o.message += "\n" + e;
		return o;
	}
	gfx::rg::GraphAsset& g = scene.graph();
	o.before = Json::parse(gfx::rg::graphToString(g.desc()));
	const bool ok = g.set(std::move(desc), scene.compileOptions());
	o.message = "グラフをコンパイルできません。ビューポートは前のグラフのままです";
	for (const auto& m : g.messages())
		if (m.rfind("error", 0) == 0) o.message += "\n" + m;
	g.clearStatus();
	if (!ok)
		return o;
	o.ok     = true;
	o.after  = Json::parse(gfx::rg::graphToString(g.desc()));
	o.result = graphJson(scene);
	return o;
}

// パイプラインのアセットの state に desc を上書きする (書かれていないフィールドは今の値のまま)
EditOutcome applyPipelineEdit(ViewportRenderer& scene, const std::string& id, const Json& json)
{
	EditOutcome o;
	const gfx::rg::PipelineAsset* current = scene.pipelines().asset(id);
	if (!current || current->compute)
	{
		o.message = "パイプラインのアセット '" + id + "' を読めません";
		return o;
	}
	gfx::rg::PipelineAsset next = *current;
	o.before = structToJson(gfxStructInfo<GfxGraphicsPipelineDesc>(), &current->state);
	const ApplyResult applied = applyJson(json, next.state);
	o.warnings = applied.warnings;
	if (!applied.ok)
	{
		o.message = "値が不正です";
		for (const auto& e : applied.errors) o.message += "\n" + e;
		return o;
	}
	if (!scene.pipelines().set(id, next))
	{
		o.code    = BridgeError::Internal;
		o.message = "パイプラインを作り直せませんでした (理由はログを確認してください)。元の設定のままです";
		return o;
	}
	o.ok    = true;
	o.after = structToJson(gfxStructInfo<GfxGraphicsPipelineDesc>(), &scene.pipelines().asset(id)->state);
	o.result = { { "id", id }, { "desc", o.after }, { "warnings", applied.warnings } };
	return o;
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
	registerProjectMethods();
	registerEditMethods();
	m_recent.load();

	// ---- エンジン (描画スレッド) ----
	RenderThread::Config renderConfig;
	renderConfig.device.appName = "SunsetEditor";
	renderConfig.device.adapter = m_options.software ? GfxAdapterType::Software : GfxAdapterType::Hardware;
#ifdef STUDIO_GFX_VALIDATION
	renderConfig.device.validation = true;
#endif

	RenderThread::Callbacks callbacks;
	callbacks.message = [this](GfxMessageSeverity s, int32_t id, const std::string& text) { onEngineMessage(s, id, text); };
	callbacks.init    = [this](const RenderThread::InitResult& r) { onEngineInit(r); };
	callbacks.stats   = [this](const RenderThread::FrameStats& st) { onStats(st); };
	callbacks.log     = [this](const std::string& level, const std::string& text) { log(level.c_str(), "viewport", text); };
	callbacks.graphChanged = [this](uint64_t revision) { m_host.emit("graph.changed", { { "revision", revision } }); };
	m_render.start(renderConfig, std::move(callbacks));
	m_render.attachViewport(&m_viewport);

	// ×ボタンで閉じるときは、ビューポートのスワップチェーンを先に解放してからウィンドウを閉じる
	m_host.setCloseHandler([this](std::function<void()> proceed) {
		m_render.detachViewport([this, proceed] { m_host.post(proceed); });
	});

	// ---- プロジェクト: スモークテストは一時フォルダにテンプレートから作る。コマンドラインで渡されたものを開く ----
	if (m_options.smokeTest)
	{
		std::error_code ec;
		m_smokeDir = std::filesystem::temp_directory_path() / ("sunset_editor_smoke_" + std::to_string(_getpid()));
		std::filesystem::remove_all(m_smokeDir, ec);
		m_host.bridge().callLocal("project.create",
			{ { "location", pathToUtf8(m_smokeDir) }, { "name", "Smoke" }, { "template", "deferred" }, { "remember", false } },
			[this](int status, const Json& r) {
				m_host.post([this, status, r] {
					if (status != 0)
					{
						log("error", "smoke", "テンプレートからプロジェクトを作れません: " + r.dump());
						m_smokeProjectFailed = true;
					}
					checkSmokeTest();
				});
			});
	}
	else if (!m_options.projectFile.empty())
		m_host.bridge().callLocal("project.open", { { "file", pathToUtf8(m_options.projectFile) } }, [this](int status, const Json& r) {
			if (status != 0)
				m_host.post([this, r] { log("error", "project", "プロジェクトを開けません: " + r.value("message", r.dump())); });
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
	if (!m_smokeDir.empty())
	{
		std::error_code ec;
		std::filesystem::remove_all(m_smokeDir, ec);
	}
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

	// ---- パイプラインのアセット (描画スレッドが持つので、描画スレッドで処理して返答する) ----
	//  id はアセットのパス ("pipelines/gbuffer.json")。desc はアセットの state (GfxGraphicsPipelineDesc)。
	//  RTV / DSV のフォーマットはパスの出力から決まるので、ここで変えても使われない
	auto noScene = [](const Bridge::Reply& r) {
		r.error(BridgeError::Unavailable, "ビューポートのシーンがありません (エンジンの初期化に失敗した可能性があります)");
	};

	b.registerMethod("pipeline.list", [this, noScene](const Json&, Bridge::Reply r) {
		m_render.postScene([r, noScene](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return noScene(r);
			Json list = Json::array();
			for (const auto& path : scene->pipelineFiles())
			{
				const gfx::rg::PipelineAsset* a = scene->pipelines().asset(path);
				if (a && !a->compute)
					list.push_back({ { "id", path }, { "label", path }, { "type", "GfxGraphicsPipelineDesc" } });
			}
			r.ok(list);
		});
	});

	b.registerMethod("pipeline.get", [this, noScene](const Json& p, Bridge::Reply r) {
		const std::string id = p.at("id").get<std::string>();
		m_render.postScene([r, id, noScene](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return noScene(r);
			const gfx::rg::PipelineAsset* a = scene->pipelines().asset(id);
			if (!a || a->compute)
				return r.error(BridgeError::InvalidParams, "パイプラインのアセット '" + id + "' を読めません (理由はログ)");
			r.ok({ { "id", id }, { "type", "GfxGraphicsPipelineDesc" },
			       { "desc", structToJson(gfxStructInfo<GfxGraphicsPipelineDesc>(), &a->state) } });
		});
	});

	// desc は変えるフィールドだけでもよい (書かれていないフィールドは今の値のまま)。
	// 値が不正ならエラー (フィールドのパス付き)、PSO を作れなければエラー (理由はログ) で、どちらも元のまま。
	// ファイルには書かない (pipeline.save で書く)
	//  label は履歴に出す名前 (省略可)
	b.registerMethod("pipeline.update", [this, noScene](const Json& p, Bridge::Reply r) {
		const std::string id    = p.at("id").get<std::string>();
		const Json        json  = p.at("desc");
		const std::string label = p.value("label", id + " を編集");
		m_render.postScene([this, r, id, json, label, noScene](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return noScene(r);
			EditOutcome o = applyPipelineEdit(*scene, id, json);
			for (const auto& w : o.warnings)
				log("warning", "inspector", id + ": " + w);
			if (!o.ok)
			{
				log("error", "inspector", id + ": " + o.message);
				return r.error(o.code, o.message);
			}
			recordEdit({ label, "pipeline", id, std::move(o.before), std::move(o.after) });
			r.ok(o.result);
		});
	});

	b.registerMethod("pipeline.save", [this, noScene](const Json& p, Bridge::Reply r) {
		const std::string id = p.at("id").get<std::string>();
		m_render.postScene([this, r, id, noScene](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return noScene(r);
			if (!scene->pipelines().save(id))
				return r.error(BridgeError::Internal, "保存できませんでした: " + id + " (理由はログ)");
			log("info", "inspector", "保存しました: " + id);
			r.ok({ { "id", id } });
		});
	});

	// ---- ビューポートのレンダーグラフ ----
	b.registerMethod("graph.get", [this, noScene](const Json&, Bridge::Reply r) {
		m_render.postScene([r, noScene](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return noScene(r);
			r.ok(graphJson(*scene));
		});
	});

	// グラフ全体の JSON (graph.get の graph と同じ形) に切り替える。読めない / コンパイルできなければ
	// エラー (理由とパス付き) で、ビューポートは前のグラフのまま。ファイルには書かない (graph.save で書く)
	//  label は履歴に出す名前 (省略可)
	b.registerMethod("graph.set", [this, noScene](const Json& p, Bridge::Reply r) {
		const std::string text  = p.at("graph").dump();
		const std::string label = p.value("label", std::string("グラフを編集"));
		m_render.postScene([this, r, text, label, noScene](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return noScene(r);
			EditOutcome o = applyGraphEdit(*scene, text);
			if (!o.ok)
				return r.error(o.code, o.message);
			m_render.notifyGraph();
			recordEdit({ label, "graph", "", std::move(o.before), std::move(o.after) });
			r.ok(o.result);
		});
	});

	b.registerMethod("graph.save", [this, noScene](const Json&, Bridge::Reply r) {
		m_render.postScene([this, r, noScene](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return noScene(r);
			gfx::rg::GraphAsset& g = scene->graph();
			const bool ok = g.save();
			std::string message = "保存できませんでした";
			for (const auto& m : g.messages()) message += "\n" + m;
			g.clearStatus();
			if (!ok)
				return r.error(BridgeError::Internal, message);
			log("info", "graph", "保存しました: " + g.file().filename().string());
			r.ok({ { "revision", g.revision() } });
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
	Json passes = Json::array();
	for (const auto& t : s.passes)
		passes.push_back({ { "name", t.name }, { "gpuMs", t.gpuMs } });
	m_host.emit("stats", {
		{ "fps",     s.fps },
		{ "frameMs", s.frameMs },
		{ "cpuMs",   s.cpuMs },
		{ "width",   s.width },
		{ "height",  s.height },
		{ "frames",  s.frames },
		{ "gpuMs",   s.gpuMs },
		{ "passes",  passes },
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
	case SmokeStage::WaitProject:
		if (m_smokeProjectFailed)
			return finish(8, "テンプレートからプロジェクトを作って開けない");
		if (!m_project)
			return;
		m_smokeStage = SmokeStage::WaitFrames;
		[[fallthrough]];
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
				m_smokeStage = SmokeStage::Graph;
				runGraphSelfCheck([this, finish](bool ok2, const std::string& error2) {
					m_host.post([this, finish, ok2, error2] {
						if (!ok2)
							return finish(7, "グラフの経路の確認に失敗: " + error2);
						m_smokeStage     = SmokeStage::WaitAfter;
						m_smokeFrameMark = m_framesDrawn;
						checkSmokeTest();
					});
				});
			});
		});
		return;
	case SmokeStage::WaitAfter:
		// パイプラインとグラフを作り直したあとも描けていること
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

	const std::string id = "pipelines/gbuffer.json";
	b.callLocal("pipeline.update", { { "id", id }, { "desc", change } }, [this, done, invalid, restore, id](int status, const Json& r) {
		if (status != 0 || r["desc"]["rasterizerState"]["fillMode"] != "Wireframe")
			return done(false, "変更が反映されない: " + r.dump());
		log("info", "smoke", "pipeline.update: ワイヤーフレームに変更");
		m_host.bridge().callLocal("pipeline.update", { { "id", id }, { "desc", invalid } }, [this, done, restore, id](int status2, const Json& r2) {
			if (status2 == 0 || r2.value("code", 0) != static_cast<int>(BridgeError::InvalidParams))
				return done(false, "不正な値がエラーにならない: " + r2.dump());
			m_host.bridge().callLocal("pipeline.update", { { "id", id }, { "desc", restore } }, [done](int status3, const Json& r3) {
				if (status3 != 0 || r3["desc"]["rasterizerState"]["cullMode"] != "Back")
					return done(false, "元に戻せない: " + r3.dump());
				done(true, {});
			});
		});
	});
}
// graph.get / graph.set を、フロントエンドと同じ経路で呼ぶ
//   1. Grid のパスを無効にすると、実行するパスから外れる
//   2. コンパイルできないグラフ (Composite のパイプラインがない) はエラーで、前のまま
//   3. 元に戻す
void EditorApp::runGraphSelfCheck(std::function<void(bool ok, const std::string& error)> done)
{
	m_host.bridge().callLocal("graph.get", Json::object(), [this, done](int status, const Json& g) {
		if (status != 0)
			return done(false, "graph.get: " + g.dump());
		const Json original = g["graph"];
		auto withPass = [&](const std::string& name, const char* key, const Json& value) {
			Json j = original;
			for (auto& p : j["passes"])
				if (p["name"] == name)
					p[key] = value;
			return j;
		};
		const Json noGrid     = withPass("Grid", "enabled", false);
		const Json noLighting = withPass("Lighting", "enabled", false);   // 深度を SRV で読まない形 (DSV / DSVRead だけ)
		const Json broken     = withPass("Composite", "pipeline", "");
		Bridge& b = m_host.bridge();
		b.callLocal("graph.set", { { "graph", noGrid } }, [this, done, noLighting, broken, original](int s1, const Json& r1) {
			const auto& order = r1["order"];
			if (s1 != 0 || std::find(order.begin(), order.end(), "Grid") != order.end())
				return done(false, "Grid を無効にできない: " + r1.dump());
			log("info", "smoke", "graph.set: Grid のパスを無効に変更");
			m_host.bridge().callLocal("graph.set", { { "graph", noLighting } }, [this, done, broken, original](int s2, const Json& r2) {
				if (s2 != 0 || r2["order"].size() != 3)
					return done(false, "Lighting を無効にできない: " + r2.dump());
				log("info", "smoke", "graph.set: Lighting のパスを無効に変更");
				m_host.bridge().callLocal("graph.set", { { "graph", broken } }, [this, done, original](int s3, const Json& r3) {
					if (s3 == 0 || r3.value("code", 0) != static_cast<int>(BridgeError::InvalidParams))
						return done(false, "コンパイルできないグラフがエラーにならない: " + r3.dump());
					m_host.bridge().callLocal("graph.set", { { "graph", original } }, [this, done](int s4, const Json& r4) {
						if (s4 != 0 || r4["order"].size() != 4)
							return done(false, "元に戻せない: " + r4.dump());
						// 元に戻す (Lighting を無効にした状態) → やり直す (元のグラフ)
						Bridge& br = m_host.bridge();
						br.callLocal("edit.undo", Json::object(), [this, done](int u1, const Json& ru1) {
						if (u1 != 0)
							return done(false, "edit.undo: " + ru1.dump());
						m_host.bridge().callLocal("graph.get", Json::object(), [this, done](int g1, const Json& rg1) {
						if (g1 != 0 || rg1["order"].size() != 3)
							return done(false, "元に戻したグラフが違う: " + rg1.dump());
						m_host.bridge().callLocal("edit.redo", Json::object(), [this, done](int u2, const Json& ru2) {
						if (u2 != 0 || ru2["state"]["canRedo"] != false)
							return done(false, "edit.redo: " + ru2.dump());
						m_host.bridge().callLocal("graph.get", Json::object(), [this, done](int g2, const Json& rg2) {
						if (g2 != 0 || rg2["order"].size() != 4)
							return done(false, "やり直したグラフが違う: " + rg2.dump());
						log("info", "smoke", "edit.undo / edit.redo: グラフを戻してやり直した");
						// 保存はプロジェクトの Assets/ に書く
						m_host.bridge().callLocal("graph.save", Json::object(), [this, done](int s5, const Json& r5) {
							if (s5 != 0)
								return done(false, "graph.save: " + r5.dump());
							m_host.post([this, done] {
								std::error_code ec;
								const auto file = m_project ? m_project->assetRoot() / pathFromUtf8(m_project->desc.viewportGraph) : std::filesystem::path();
								const bool inProject = !file.empty() && std::filesystem::exists(file, ec) && !m_smokeDir.empty()
									&& pathToUtf8(file).rfind(pathToUtf8(m_smokeDir), 0) == 0;
								if (!inProject)
									return done(false, "graph.save がプロジェクトに書いていない: " + pathToUtf8(file));
								log("info", "smoke", "graph.save: プロジェクトに保存");
								done(true, {});
							});
						});
						});
						});
						});
						});
					});
				});
			});
		});
	});
}
// ============================================================
//  プロジェクト (M4a)
// ============================================================
Json EditorApp::projectJson() const
{
	if (!m_project)
		return { { "open", false } };
	return {
		{ "open",      true },
		{ "name",      m_project->desc.name },
		{ "file",      pathToUtf8(m_project->file) },
		{ "dir",       pathToUtf8(m_project->dir()) },
		{ "assetRoot", pathToUtf8(m_project->assetRoot()) },
		{ "engine",    m_project->desc.engine },
		{ "graph",     m_project->desc.viewportGraph },
	};
}

void EditorApp::openProject(const std::filesystem::path& file, std::function<void(bool, const Json&, const std::string&)> done)
{
	if (m_projectBusy)
		return done(false, {}, "ほかのプロジェクトを開いています");
	Project project;
	ProjectDiagnostics diag;
	if (!loadProject(file, project, diag, STUDIO_GFX_VERSION))
	{
		std::string message = "プロジェクトを開けません: " + pathToUtf8(file);
		for (const auto& e : diag.errors) message += "\n" + e;
		return done(false, {}, message);
	}
	for (const auto& w : diag.warnings)
		log("warning", "project", w);

	m_projectBusy = true;
	m_render.openProject(project.assetRoot(), project.desc.viewportGraph,
		[this, project, done](bool ok, const std::string& error) {
			// 描画スレッドから呼ばれる
			m_host.post([this, project, done, ok, error] {
				m_projectBusy = false;
				if (!ok)
				{
					m_project.reset();
					m_host.setTitle("SunsetEditor");
					m_host.emit("project.changed", projectJson());
					return done(false, {}, "プロジェクトのビューポートを作れません: " + error);
				}
				m_project = project;
				m_undo.clear();
				emitEditState();
				m_host.setTitle(project.desc.name + " - SunsetEditor");
				log("info", "project", "プロジェクトを開きました: " + pathToUtf8(project.file));
				const Json info = projectJson();
				m_host.emit("project.changed", info);
				done(true, info, {});
			});
		});
}

void EditorApp::registerProjectMethods()
{
	Bridge& b = m_host.bridge();
	auto reply = [](Bridge::Reply r) {
		return [r](bool ok, const Json& result, const std::string& error) {
			if (ok) r.ok(result);
			else    r.error(BridgeError::InvalidParams, error);
		};
	};

	b.registerMethod("project.info", [this](const Json&, Bridge::Reply r) { r.ok(projectJson()); });

	b.registerMethod("project.recent", [this](const Json&, Bridge::Reply r) {
		m_recent.load();
		Json list = Json::array();
		for (const auto& e : m_recent.entries())
			list.push_back({ { "file", pathToUtf8(e.file) }, { "name", e.name }, { "exists", e.exists } });
		const std::filesystem::path docs = documentsFolder();
		r.ok({ { "projects", list }, { "defaultLocation", docs.empty() ? std::string() : pathToUtf8(docs / "SunsetStudio") } });
	});

	b.registerMethod("project.templates", [this](const Json&, Bridge::Reply r) {
		Json list = Json::array();
		std::error_code ec;
		for (const auto& e : std::filesystem::directory_iterator(m_options.templateRoot, ec))
			if (e.is_directory(ec))
			{
				const std::string id = pathToUtf8(e.path().filename());
				list.push_back({ { "id", id }, { "label", id == "deferred" ? "ディファード (G-Buffer → ライティング → グリッド → 合成)" : id } });
			}
		r.ok(list);
	});

	b.registerMethod("project.open", [this, reply](const Json& p, Bridge::Reply r) {
		const std::filesystem::path file = pathFromUtf8(p.at("file").get<std::string>());
		openProject(file, [this, r, reply](bool ok, const Json& result, const std::string& error) {
			if (ok)
			{
				m_recent.add(*m_project);
				m_recent.save();
			}
			reply(r)(ok, result, error);
		});
	});

	// location の下に <name>/<name>.sunsetproj を作り、テンプレートを Assets/ にコピーして開く
	b.registerMethod("project.create", [this, reply](const Json& p, Bridge::Reply r) {
		const std::string           name     = p.at("name").get<std::string>();
		const std::filesystem::path location = pathFromUtf8(p.at("location").get<std::string>());
		const std::string           tmpl     = p.value("template", std::string("deferred"));
		const bool                  remember = p.value("remember", true);
		if (tmpl.find_first_of("/\\.") != std::string::npos)
			return r.error(BridgeError::InvalidParams, "テンプレートの名前が不正です: " + tmpl);
		Project created;
		std::string error;
		if (!createProject(location, name, m_options.templateRoot / pathFromUtf8(tmpl), STUDIO_GFX_VERSION, created, error))
			return r.error(BridgeError::InvalidParams, error);
		log("info", "project", "プロジェクトを作りました: " + pathToUtf8(created.file));
		openProject(created.file, [this, r, reply, remember](bool ok, const Json& result, const std::string& err) {
			if (ok && remember)
			{
				m_recent.add(*m_project);
				m_recent.save();
			}
			reply(r)(ok, result, err);
		});
	});

	b.registerMethod("project.close", [this](const Json&, Bridge::Reply r) {
		if (m_projectBusy)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いています");
		m_render.closeProject([this, r] {
			m_host.post([this, r] {
				m_project.reset();
				m_undo.clear();
				emitEditState();
				m_host.setTitle("SunsetEditor");
				m_host.emit("project.changed", projectJson());
				r.ok(projectJson());
			});
		});
	});

	// パネルの配置 (UI が決めた JSON をそのまま Saved/Layout.json に置く)
	b.registerMethod("project.layout.get", [this](const Json&, Bridge::Reply r) {
		if (!m_project)
			return r.ok({ { "layout", nullptr } });
		std::ifstream f(m_project->layoutFile(), std::ios::binary);
		if (!f)
			return r.ok({ { "layout", nullptr } });
		std::stringstream ss;
		ss << f.rdbuf();
		try
		{
			r.ok({ { "layout", Json::parse(ss.str()) } });
		}
		catch (const std::exception&)
		{
			r.ok({ { "layout", nullptr } });   // 壊れていたら既定の配置
		}
	});
	b.registerMethod("project.layout.set", [this](const Json& p, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		std::error_code ec;
		std::filesystem::create_directories(m_project->savedDir(), ec);
		std::ofstream f(m_project->layoutFile(), std::ios::binary | std::ios::trunc);
		if (!f || !(f << p.at("layout").dump(1)))
			return r.error(BridgeError::Internal, "書き込めません: " + pathToUtf8(m_project->layoutFile()));
		r.ok();
	});

	// ---- OS のダイアログ (UI スレッドで、閉じるまで戻らない) ----
	b.registerMethod("dialog.openProject", [this](const Json&, Bridge::Reply r) {
		const auto file = openProjectDialog(m_host.nativeWindow());
		r.ok({ { "file", file ? Json(pathToUtf8(*file)) : Json(nullptr) } });
	});
	b.registerMethod("dialog.pickFolder", [this](const Json& p, Bridge::Reply r) {
		const auto folder = pickFolderDialog(m_host.nativeWindow(), pathFromUtf8(p.value("initial", std::string())));
		r.ok({ { "folder", folder ? Json(pathToUtf8(*folder)) : Json(nullptr) } });
	});
}
// ============================================================
//  編集の履歴 (M4)
// ============================================================
void EditorApp::recordEdit(UndoStack::Entry entry)
{
	m_host.post([this, entry = std::move(entry)]() mutable {
		if (m_undo.push(std::move(entry)))
			emitEditState();
	});
}

void EditorApp::emitEditState()
{
	m_host.emit("edit.changed", m_undo.state());
}

void EditorApp::registerEditMethods()
{
	Bridge& b = m_host.bridge();

	b.registerMethod("edit.state", [this](const Json&, Bridge::Reply r) { r.ok(m_undo.state()); });

	// 元に戻す / やり直す: 履歴の before / after を、編集と同じ経路で適用する。
	// 適用できなければ (ファイルの外部での変更で、もう成り立たないなど) エラーで、履歴はそのまま
	auto step = [this](bool undo, Bridge::Reply r) {
		if (m_editBusy)
			return r.error(BridgeError::Unavailable, "元に戻す / やり直すを処理しています");
		const UndoStack::Entry* top = undo ? m_undo.peekUndo() : m_undo.peekRedo();
		if (!top)
			return r.error(BridgeError::Unavailable, undo ? "元に戻す編集がありません" : "やり直す編集がありません");
		const UndoStack::Entry entry = *top;
		const Json target = undo ? entry.before : entry.after;
		m_editBusy = true;
		m_render.postScene([this, r, entry, target, undo](GraphicsEngine&, ViewportRenderer* scene) {
			EditOutcome o;
			if (!scene)
				o.message = "ビューポートのシーンがありません";
			else if (entry.kind == "graph")
			{
				o = applyGraphEdit(*scene, target.dump());
				if (o.ok) m_render.notifyGraph();
			}
			else if (entry.kind == "pipeline")
				o = applyPipelineEdit(*scene, entry.target, target);
			else
				o.message = "知らない種類の編集です: " + entry.kind;
			m_host.post([this, r, entry, undo, o] {
				m_editBusy = false;
				if (!o.ok)
				{
					log("error", "edit", std::string(undo ? "元に戻せません: " : "やり直せません: ") + entry.label + "\n" + o.message);
					return r.error(o.code, o.message);
				}
				if (undo) m_undo.didUndo();
				else      m_undo.didRedo();
				emitEditState();
				if (entry.kind == "pipeline")
					m_host.emit("pipeline.changed", { { "id", entry.target } });
				log("info", "edit", std::string(undo ? "元に戻しました: " : "やり直しました: ") + entry.label);
				r.ok({ { "label", entry.label }, { "kind", entry.kind }, { "target", entry.target }, { "state", m_undo.state() } });
			});
		});
	};
	b.registerMethod("edit.undo", [step](const Json&, Bridge::Reply r) { step(true, r); });
	b.registerMethod("edit.redo", [step](const Json&, Bridge::Reply r) { step(false, r); });
}
} // namespace studio
