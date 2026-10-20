// ============================================================
//  editor/src/app/EditorAssets.cpp
//
//  シェーダーとパイプラインのファイルを編集する (M4 のシェーダー編集。UI はシェーダー / パイプラインのタブ)。
//    asset.list / asset.read / asset.write / asset.templates / asset.create / asset.openExternal
//    shader.compile   保存していない内容でもコンパイルだけして、メッセージとリフレクションを返す (描画は変えない)
//    shader.usages    そのシェーダーを使うパイプラインとステージ
//    shader.status    ホットリロード / 読み込みでコンパイルに失敗しているシェーダー
//  イベント: shader.status (コンパイルの成否が変わった)
//  保存 (asset.write) すると、描画スレッドのホットリロード (ShaderLibrary / PipelineLibrary の poll) が反映する。
//  失敗したときは前のシェーダーのまま描く。
// ============================================================
#include "EditorApp.h"
#include "host/ExternalOpen.h"
#include "services/AssetFiles.h"
#include "services/SchemaJson.h"

#include "gfx/rendergraph/GraphAsset.h"
#include "gfx/schema/GfxSchema.h"

#include <memory>
#include <thread>

namespace studio
{
namespace
{
// コンパイルのメッセージを集める (描画スレッドで shader.compile を実行しているあいだだけ有効)
thread_local std::string* t_shaderCapture = nullptr;

template<typename T>
Json arrayJson(const T* items, uint32_t count, uint32_t max)
{
	Json a = Json::array();
	for (uint32_t i = 0; i < count && i < max; ++i)
		a.push_back(structToJson(gfxStructInfo<T>(), &items[i]));
	return a;
}

Json reflectionJson(const GfxShaderReflection& r)
{
	return {
		{ "shaderModel", r.shaderModel },
		{ "threadGroupSize", { r.threadGroupSize[0], r.threadGroupSize[1], r.threadGroupSize[2] } },
		{ "instructionCount", r.instructionCount },
		{ "truncated", r.truncated },
		{ "bindings", arrayJson(r.bindings, r.bindingCount, GFX_MAX_SHADER_BINDINGS) },
		{ "cbuffers", arrayJson(r.cbuffers, r.cbufferCount, GFX_MAX_SHADER_CBUFFERS) },
		{ "variables", arrayJson(r.variables, r.variableCount, GFX_MAX_SHADER_VARIABLES) },
		{ "inputs", arrayJson(r.inputs, r.inputCount, GFX_MAX_SHADER_SIGNATURE) },
		{ "outputs", arrayJson(r.outputs, r.outputCount, GFX_MAX_SHADER_SIGNATURE) },
	};
}

ShaderStage stageOf(const std::string& s)
{
	return s == "vs" ? ShaderStage::Vertex : s == "cs" ? ShaderStage::Compute : ShaderStage::Fragment;
}

// エンジンのメッセージの 1 行目 "<what>: シェーダーのコンパイルに失敗しました (<path>)" からパスを取り出す
bool failedShaderPath(const std::string& text, std::string& path)
{
	const std::string mark = "シェーダーのコンパイルに失敗しました (";
	const size_t eol = text.find('\n');
	const std::string first = text.substr(0, eol);
	const size_t p = first.find(mark);
	if (p == std::string::npos || first.empty() || first.back() != ')')
		return false;
	path = first.substr(p + mark.size(), first.size() - 1 - (p + mark.size()));
	return !path.empty();
}
} // namespace

// 描画スレッドから (onEngineMessage)。shader.compile の最中なら集めるだけで、ログには出さない
bool EditorApp::captureShaderMessage(int32_t id, const std::string& text)
{
	if (id != GFX_MESSAGE_ID_SHADER_COMPILE)
		return false;
	if (t_shaderCapture)
	{
		if (!t_shaderCapture->empty())
			*t_shaderCapture += "\n";
		*t_shaderCapture += text;
		return true;
	}
	std::string path;
	if (failedShaderPath(text, path))
		m_host.post([this, path, text] { onShaderStatus(path, false, text); });
	return false;
}

// UI スレッド。path はエンジンが使った (絶対) パス
void EditorApp::onShaderStatus(const std::string& enginePath, bool ok, const std::string& text)
{
	if (!m_project)
		return;
	const std::string rel = assetRelativePath(m_project->assetRoot(), gfx::rg::pathFromUtf8(enginePath));
	if (rel.empty())
		return;
	if (ok)
	{
		if (m_shaderErrors.erase(rel) == 0)
			return;
	}
	else
		m_shaderErrors[rel] = text;
	m_host.emit("shader.status", { { "path", rel }, { "ok", ok }, { "text", ok ? std::string() : text } });
}

void EditorApp::registerAssetMethods()
{
	Bridge& b = m_host.bridge();

	// プロジェクトのアセットのフォルダの中の絶対パス (UI スレッド)
	auto resolve = [this](const Json& p, Bridge::Reply& r, std::filesystem::path& out, std::string& rel) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません"), false;
		rel = p.value("path", std::string());
		std::string error;
		if (!resolveAssetPath(m_project->assetRoot(), rel, out, error))
			return r.error(BridgeError::InvalidParams, error), false;
		rel = assetRelativePath(m_project->assetRoot(), out);
		return true;
	};

	b.registerMethod("asset.list", [this](const Json& p, Bridge::Reply r) {
		if (!m_project)
			return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
		std::vector<std::string> exts;
		if (p.contains("extensions") && p.at("extensions").is_array())
			for (const auto& e : p.at("extensions"))
				if (e.is_string())
					exts.push_back(e.get<std::string>());
		r.ok({ { "files", listAssetFiles(m_project->assetRoot(), p.value("dir", std::string()), exts) } });
	});

	b.registerMethod("asset.read", [resolve](const Json& p, Bridge::Reply r) {
		std::filesystem::path file;
		std::string rel;
		if (!resolve(p, r, file, rel))
			return;
		std::string text;
		if (!gfx::rg::readTextFile(file, text))
			return r.error(BridgeError::InvalidParams, "ファイルを読めません: " + rel);
		r.ok({ { "path", rel }, { "text", text } });
	});

	// 保存する (原子的に置き換える)。描画スレッドのホットリロードが反映する。create = true なら新しいファイルだけ
	b.registerMethod("asset.write", [this, resolve](const Json& p, Bridge::Reply r) {
		std::filesystem::path file;
		std::string rel;
		if (!resolve(p, r, file, rel))
			return;
		if (!p.contains("text") || !p.at("text").is_string())
			return r.error(BridgeError::InvalidParams, "text がありません");
		std::error_code ec;
		if (p.value("create", false) && std::filesystem::exists(file, ec))
			return r.error(BridgeError::InvalidParams, "すでにあります: " + rel);
		std::filesystem::create_directories(file.parent_path(), ec);
		std::string error;
		if (!gfx::rg::writeTextFileAtomic(file, p.at("text").get<std::string>(), &error))
			return r.error(BridgeError::Internal, "保存できません: " + rel + (error.empty() ? "" : " (" + error + ")"));
		log("info", "asset", "保存しました: " + rel);
		m_render.wake();
		r.ok({ { "path", rel } });
	});

	b.registerMethod("asset.templates", [](const Json&, Bridge::Reply r) {
		Json list = Json::array();
		for (const auto& t : assetTemplates())
			list.push_back({ { "id", t.id }, { "kind", t.kind }, { "label", t.label }, { "folder", t.folder },
			                 { "suffix", t.suffix }, { "stage", t.stage }, { "description", t.description } });
		r.ok({ { "templates", list } });
	});

	// テンプレートから新しいファイルを作る (同じ名前があればエラー)。パイプラインは shader に PS / CS のパス
	// (描画ノードのパイプラインは vs に VS のパスも)
	b.registerMethod("asset.create", [this, resolve](const Json& p, Bridge::Reply r) {
		std::filesystem::path file;
		std::string rel;
		if (!resolve(p, r, file, rel))
			return;
		std::string text;
		if (!assetTemplateText(p.value("template", std::string()), p.value("shader", std::string()), text, p.value("vs", std::string())))
			return r.error(BridgeError::InvalidParams, "テンプレートがありません: " + p.value("template", std::string()));
		std::error_code ec;
		if (std::filesystem::exists(file, ec))
			return r.error(BridgeError::InvalidParams, "すでにあります: " + rel);
		std::filesystem::create_directories(file.parent_path(), ec);
		std::string error;
		if (!gfx::rg::writeTextFileAtomic(file, text, &error))
			return r.error(BridgeError::Internal, "作れません: " + rel + (error.empty() ? "" : " (" + error + ")"));
		log("info", "asset", "作りました: " + rel);
		r.ok({ { "path", rel }, { "text", text } });
	});

	// 外部のアプリで開く (app = "default" / "vscode")。起動を待つので別のスレッドで
	b.registerMethod("asset.openExternal", [resolve](const Json& p, Bridge::Reply r) {
		std::filesystem::path file;
		std::string rel;
		if (!resolve(p, r, file, rel))
			return;
		const std::string app  = p.value("app", std::string("default"));
		const int         line = p.value("line", 0);
		std::thread([r, file, app, line] {
			std::string error;
			const bool ok = app == "vscode" ? openInVSCode(file, line, error) : openWithDefaultApp(file, error);
			if (ok)
				r.ok({ { "opened", true } });
			else
				r.error(BridgeError::Unavailable, error);
		}).detach();
	});

	// コンパイルだけする (描画には使わない)。text がなければファイルの内容。stage は "vs" / "ps" / "cs" (省略時は名前から推測)
	b.registerMethod("shader.compile", [this, resolve](const Json& p, Bridge::Reply r) {
		std::filesystem::path file;
		std::string rel;
		if (!resolve(p, r, file, rel))
			return;
		std::string text;
		if (p.contains("text") && p.at("text").is_string())
			text = p.at("text").get<std::string>();
		else if (!gfx::rg::readTextFile(file, text))
			return r.error(BridgeError::InvalidParams, "ファイルを読めません: " + rel);
		std::string stage = p.value("stage", guessShaderStage(rel));
		if (stage != "vs" && stage != "ps" && stage != "cs")
			return r.error(BridgeError::InvalidParams, "ステージ (vs / ps / cs) が分かりません: " + rel);
		const std::string entry   = p.value("entry", std::string("main"));
		const std::string profile = p.value("profile", std::string());
		const std::filesystem::path shadersDir = m_project->assetRoot() / "shaders";
		m_render.post([r, rel, file, text, stage, entry, profile, shadersDir](GraphicsEngine& engine) {
			if (!engine.isInitialized())
				return r.error(BridgeError::Unavailable, "エンジンが初期化されていません");
			const std::string sourceName = pathToUtf8(file);
			const std::string dir1 = pathToUtf8(file.parent_path()), dir2 = pathToUtf8(shadersDir);
			const char* includes[] = { dir1.c_str(), dir2.c_str() };
			ShaderDesc d;
			d.stage           = stageOf(stage);
			d.source          = text.c_str();
			d.entryPoint      = entry.c_str();
			d.sourceName      = sourceName.c_str();
			d.targetProfile   = profile.empty() ? nullptr : profile.c_str();
			d.includeDirs     = includes;
			d.includeDirCount = 2;
			std::string messages;
			t_shaderCapture = &messages;
			const ShaderHandle h = engine.compileShader(d);
			t_shaderCapture = nullptr;
			Json out = { { "path", rel }, { "stage", stage }, { "ok", h.valid() }, { "messages", messages } };
			if (h.valid())
			{
				auto refl = std::make_unique<GfxShaderReflection>();
				if (engine.getShaderReflection(h, *refl))
					out["reflection"] = reflectionJson(*refl);
				engine.destroyShader(h);
			}
			r.ok(out);
		});
	});

	// そのシェーダーを使うパイプライン (ステージ、エントリー、プロファイル)
	b.registerMethod("shader.usages", [this](const Json& p, Bridge::Reply r) {
		const std::string path = p.value("path", std::string());
		m_render.postScene([r, path](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
			Json list = Json::array();
			for (const std::string& id : scene->pipelineFiles())
				if (const gfx::rg::PipelineAsset* a = scene->pipelines().asset(id))
				{
					const std::pair<const char*, const gfx::rg::ShaderStageAsset*> stages[] = { { "vs", &a->vs }, { "ps", &a->ps }, { "cs", &a->cs } };
					for (const auto& st : stages)
						if (st.second->file == path)
							list.push_back({ { "pipeline", id }, { "stage", st.first }, { "entry", st.second->entry }, { "profile", st.second->profile } });
				}
			r.ok({ { "usages", list } });
		});
	});

	b.registerMethod("shader.status", [this](const Json&, Bridge::Reply r) {
		Json list = Json::array();
		for (const auto& [path, text] : m_shaderErrors)
			list.push_back({ { "path", path }, { "ok", false }, { "text", text } });
		r.ok({ { "errors", list } });
	});
}
} // namespace studio
