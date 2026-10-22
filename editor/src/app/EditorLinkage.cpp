// ============================================================
//  editor/src/app/EditorLinkage.cpp
//
//  パスの入力 / 出力と、パイプライン、シェーダーの宣言のつながり (2026-10-22)。
//    pass.linkage  { pass } → { pipeline, stages: [{ stage, file }], inputs: [{ index, slot?, var?, status }],
//                  outputs: [{ index, target? / slot?, var?, status }], unconnected: [{ name, type, cls, reg, space, used, file, line }],
//                  targets: [{ index, name, line, connected }] }
//                  status: "ok" / "noDecl" (届く先の宣言がない) / "unused" (宣言しているがシェーダーが使っていない) /
//                          "noSlot" (bind がテーブルの外) / "none" (シェーダーの変数を使わない使い方)
//    pass.declare  { pass, kind: "input" | "output", index } → シェーダーのファイルに宣言を書き足す (ホットリロードで反映)
//                  入力 (SRV) は `Texture2D <名前> : register(tN);`、出力の RTV は SV_Target のメンバー、UAV は RWTexture2D
//  シェーダーの宣言は、ファイル (と 1 段の #include) の文字と、リフレクションの両方から集める (services/PassLinkage.h)。
// ============================================================
#include "EditorApp.h"

#include "services/PassLinkage.h"

#include "gfx/rendergraph/GraphAsset.h"
#include "gfx/schema/GfxSchema.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <regex>
#include <set>

namespace studio
{
namespace
{
struct StageText
{
	std::string stage;   // "vs" / "ps" / "cs"
	std::string file;    // アセットのパス
	std::string text;
};

char clsOf(GfxShaderInputType t)
{
	switch (t)
	{
	case GfxShaderInputType::CBuffer: return 'b';
	case GfxShaderInputType::Sampler: return 's';
	case GfxShaderInputType::Texture:
	case GfxShaderInputType::TBuffer:
	case GfxShaderInputType::Structured:
	case GfxShaderInputType::ByteAddress:
	case GfxShaderInputType::RTAccelerationStructure: return 't';
	default: return 'u';
	}
}

std::string regName(char cls, uint32_t reg, uint32_t space)
{
	return std::string(1, cls) + std::to_string(reg) + (space ? " (space" + std::to_string(space) + ")" : "");
}

// パスと、そのパイプラインの宣言を集める
struct PassInfo
{
	const gfx::rg::PassDesc*       pass  = nullptr;
	const gfx::rg::PipelineAsset*  asset = nullptr;
	std::vector<StageText>         stages;
	std::vector<ShaderDecl>        decls;     // ファイルとリフレクションをまとめたもの
	std::vector<TargetDecl>        targets;   // PS の SV_Target
	std::set<uint32_t>             graphBound;
	std::string                    error;
};

// reflect = false ならファイルの文字だけ読む (シェーダーをコンパイルしない)
PassInfo collectFor(ViewportRenderer& scene, const gfx::rg::PassDesc& pass, bool reflect = true)
{
	PassInfo info;
	info.pass = &pass;
	for (const auto& c : info.pass->constants) if (c.bind.valid()) info.graphBound.insert(static_cast<uint32_t>(c.bind.param));
	for (const auto* refs : { &info.pass->inputs, &info.pass->outputs })
		for (const auto& r : *refs) if (r.bind.valid()) info.graphBound.insert(static_cast<uint32_t>(r.bind.param));
	if (info.pass->pipeline.empty())
		return info;   // Copy のパス
	info.asset = scene.pipelines().asset(info.pass->pipeline);
	if (!info.asset)
		return info.error = "パイプライン '" + info.pass->pipeline + "' を読めません", info;

	struct Stage { const char* name; const gfx::rg::ShaderStageAsset* asset; ShaderStage stage; };
	std::vector<Stage> stages;
	if (info.asset->compute)
		stages.push_back({ "cs", &info.asset->cs, ShaderStage::Compute });
	else
	{
		stages.push_back({ "vs", &info.asset->vs, ShaderStage::Vertex });
		stages.push_back({ "ps", &info.asset->ps, ShaderStage::Fragment });
	}
	static const std::regex inc(R"re(^\s*#\s*include\s*"([^"]+)")re");
	for (const Stage& sg : stages)
	{
		const std::string stage = sg.name;
		const gfx::rg::ShaderStageAsset& st = *sg.asset;
		if (!st.used())
			continue;
		StageText t{ stage, st.file, {} };
		gfx::rg::readTextFile(scene.assetRoot() / gfx::rg::pathFromUtf8(st.file), t.text);
		for (auto& d : parseShaderDecls(t.text, st.file))
			info.decls.push_back(d);
		// 1 段の #include (ファイルと同じフォルダからの相対パス)
		const std::string dir = st.file.find('/') != std::string::npos ? st.file.substr(0, st.file.rfind('/') + 1) : std::string();
		std::istringstream lines(t.text);
		for (std::string line; std::getline(lines, line);)
		{
			std::smatch m;
			if (!std::regex_search(line, m, inc))
				continue;
			const std::string rel = dir + m[1].str();
			std::string text;
			if (gfx::rg::readTextFile(scene.assetRoot() / gfx::rg::pathFromUtf8(rel), text))
				for (auto& d : parseShaderDecls(text, rel))
					info.decls.push_back(d);
		}
		if (stage == "ps")
			info.targets = parseTargets(t.text);
		// リフレクション: 使っている変数に印。ファイルで分からなかったもの (register を書いていない) を足す
		std::string err;
		if (!reflect)
		{
			info.stages.push_back(std::move(t));
			continue;
		}
		if (auto r = scene.nodeResources().reflectStage(st, sg.stage, err))
		{
			for (uint32_t b = 0; b < r->bindingCount && b < GFX_MAX_SHADER_BINDINGS; ++b)
			{
				const GfxShaderBinding& bd = r->bindings[b];
				const char cls = clsOf(bd.type);
				auto it = std::find_if(info.decls.begin(), info.decls.end(), [&](const ShaderDecl& d) {
					return d.cls == cls && d.reg == bd.bindPoint && d.space == bd.space;
				});
				if (it != info.decls.end())
					it->used = true;
				else
				{
					ShaderDecl d;
					d.name  = bd.name;
					d.cls   = cls;
					d.reg   = bd.bindPoint;
					d.space = bd.space;
					d.used  = true;
					d.type  = cls == 't' ? "Texture" : cls == 'u' ? "RWTexture" : cls == 'b' ? "cbuffer" : "Sampler";
					info.decls.push_back(d);
				}
			}
		}
		else if (info.error.empty())
			info.error = err;
		info.stages.push_back(std::move(t));
	}
	return info;
}

PassInfo collect(ViewportRenderer& scene, const std::string& passName)
{
	for (const auto& p : scene.graph().desc().passes)
		if (p.name == passName)
			return collectFor(scene, p);
	PassInfo info;
	info.error = "パス " + passName + " はありません";
	return info;
}

const ShaderDecl* findDecl(const PassInfo& info, char cls, uint32_t reg, uint32_t space)
{
	for (const ShaderDecl& d : info.decls)
		if (d.cls == cls && d.reg == reg && d.space == space)
			return &d;
	return nullptr;
}

Json declJson(const ShaderDecl& d)
{
	return { { "name", d.name }, { "type", d.type }, { "cls", std::string(1, d.cls) }, { "reg", d.reg }, { "space", d.space },
	         { "register", regName(d.cls, d.reg, d.space) }, { "used", d.used }, { "file", d.file }, { "line", d.line } };
}

Json linkageJson(ViewportRenderer& scene, const std::string& passName)
{
	const PassInfo info = collect(scene, passName);
	Json j = { { "pass", passName } };
	if (!info.pass)
		return j["error"] = info.error, j;
	j["pipeline"] = info.pass->pipeline;
	if (!info.error.empty())
		j["error"] = info.error;
	Json stages = Json::array();
	for (const auto& s : info.stages)
		stages.push_back({ { "stage", s.stage }, { "file", s.file } });
	j["stages"] = std::move(stages);

	const GfxBindingLayoutDesc* layout = info.asset ? &info.asset->layout : nullptr;
	std::set<std::string> covered;   // つないでいるレジスタ ("t0" など)
	auto refJson = [&](const gfx::rg::PassResourceRef& r, size_t index, bool output) {
		Json x = { { "index", index } };
		const std::string as = std::string(gfx::rg::nameOf(r.as));
		if (output && r.as == gfx::rg::Access::RTV)
		{
			x["target"] = r.rtvIndex;
			const auto t = std::find_if(info.targets.begin(), info.targets.end(), [&](const TargetDecl& d) { return d.index == r.rtvIndex; });
			if (t != info.targets.end())
			{
				x["status"] = "ok";
				x["var"]    = { { "name", t->name.empty() ? "SV_Target" + std::to_string(t->index) : t->name }, { "line", t->line } };
			}
			else
				x["status"] = info.targets.empty() && info.stages.empty() ? "none" : "noDecl";
			return x;
		}
		if (!r.bind.valid() || !layout)
			return x["status"] = "none", x;
		const auto slot = tableSlotRegister(*layout, static_cast<uint32_t>(r.bind.param), r.bind.offset);
		if (!slot)
			return x["status"] = "noSlot", x;
		x["register"] = regName(slot->cls, slot->reg, slot->space);
		covered.insert(std::string(1, slot->cls) + std::to_string(slot->reg) + "/" + std::to_string(slot->space));
		const ShaderDecl* d = findDecl(info, slot->cls, slot->reg, slot->space);
		if (!d)
			return x["status"] = "noDecl", x;
		x["var"]    = declJson(*d);
		x["status"] = d->used ? "ok" : "unused";
		(void)as;
		return x;
	};
	Json inputs = Json::array(), outputs = Json::array();
	for (size_t i = 0; i < info.pass->inputs.size(); ++i)  inputs.push_back(refJson(info.pass->inputs[i], i, false));
	for (size_t i = 0; i < info.pass->outputs.size(); ++i) outputs.push_back(refJson(info.pass->outputs[i], i, true));
	j["inputs"]  = std::move(inputs);
	j["outputs"] = std::move(outputs);

	// どの入力 / 出力もつないでいないテクスチャ / UAV の宣言 (物体ごとのリソースのテーブルが指すものは除く)
	std::set<std::string> perObject;
	if (layout)
		for (uint32_t p = 0; p < layout->numParameters; ++p)
		{
			if (info.graphBound.count(p) || layout->parameters[p].parameterType != GfxRootParameterType::DescriptorTable)
				continue;
			for (uint32_t off = 0; off < 256; ++off)
			{
				const auto s = tableSlotRegister(*layout, p, off);
				if (!s) break;
				perObject.insert(std::string(1, s->cls) + std::to_string(s->reg) + "/" + std::to_string(s->space));
			}
		}
	Json unconnected = Json::array();
	for (const ShaderDecl& d : info.decls)
	{
		if (d.cls != 't' && d.cls != 'u')
			continue;
		const std::string key = std::string(1, d.cls) + std::to_string(d.reg) + "/" + std::to_string(d.space);
		if (covered.count(key) || perObject.count(key))
			continue;
		Json x = declJson(d);
		x["perObject"] = false;
		unconnected.push_back(std::move(x));
	}
	j["unconnected"] = std::move(unconnected);

	// SV_Target で、どの RTV も書いていないもの
	Json targets = Json::array();
	for (const TargetDecl& t : info.targets)
	{
		bool connected = false;
		for (const auto& o : info.pass->outputs)
			if (o.as == gfx::rg::Access::RTV && o.rtvIndex == t.index)
				connected = true;
		targets.push_back({ { "index", t.index }, { "name", t.name }, { "line", t.line }, { "connected", connected } });
	}
	j["targets"] = std::move(targets);
	return j;
}

// リソースのフォーマットから、宣言の型
std::string srvTypeFor(const gfx::rg::GraphDesc& g, const std::string& res)
{
	for (const auto& r : g.resources)
		if (r.name == res)
		{
			const std::string f = gfxEnumName(gfxEnumInfo<GfxFormat>(), static_cast<uint64_t>(r.format)) ? gfxEnumName(gfxEnumInfo<GfxFormat>(), static_cast<uint64_t>(r.format)) : "";
			if (f.rfind("D", 0) == 0 || f == "R32_Float" || f == "R16_Float") return "Texture2D<float>";
			if (f.find("Uint") != std::string::npos) return "Texture2D<uint>";
			return "Texture2D";
		}
	return "Texture2D";
}
// 出力のフォーマット (つないだリソース、未接続なら参照の format) から、SV_Target のメンバーの型
std::string rtvTypeFor(const gfx::rg::GraphDesc& g, const gfx::rg::PassResourceRef& ref)
{
	GfxFormat f = ref.viewFormat;
	for (const auto& r : g.resources)
		if (r.name == ref.res)
			f = r.format;
	const char* n = gfxEnumName(gfxEnumInfo<GfxFormat>(), static_cast<uint64_t>(f));
	return n && std::string(n).find("Uint") != std::string::npos ? "uint" : "float4";
}
} // namespace

// 定義名のない入力 / 出力に、既定の名前を入れる: 届く先のシェーダーの変数 (入力 / UAV は register の変数、
// RTV は SV_Target のメンバー)、なければつないだリソースの名前、なければ in<番号> / out<番号>。パスの中で重ならないようにする
void fillPortNames(ViewportRenderer& scene, gfx::rg::GraphDesc& graph)
{
	for (auto& pass : graph.passes)
	{
		const bool missing = std::any_of(pass.inputs.begin(), pass.inputs.end(), [](const auto& r) { return r.name.empty(); }) ||
		                     std::any_of(pass.outputs.begin(), pass.outputs.end(), [](const auto& r) { return r.name.empty(); });
		if (!missing)
			continue;
		const PassInfo info = collectFor(scene, pass, false);
		const GfxBindingLayoutDesc* layout = info.asset ? &info.asset->layout : nullptr;
		auto fill = [&](std::vector<gfx::rg::PassResourceRef>& refs, bool output) {
			std::set<std::string> taken;
			for (const auto& r : refs)
				if (!r.name.empty()) taken.insert(r.name);
			for (size_t i = 0; i < refs.size(); ++i)
			{
				auto& r = refs[i];
				if (!r.name.empty())
					continue;
				std::string name;
				if (output && r.as == gfx::rg::Access::RTV)
				{
					for (const TargetDecl& t : info.targets)
						if (t.index == r.rtvIndex && !t.name.empty()) name = t.name;
				}
				else if (r.bind.valid() && layout)
				{
					if (const auto slot = tableSlotRegister(*layout, static_cast<uint32_t>(r.bind.param), r.bind.offset))
						if (const ShaderDecl* d = findDecl(info, slot->cls, slot->reg, slot->space)) name = d->name;
				}
				if (name.empty() && !r.res.empty())
					name = shaderIdentifierFor(r.res[0] == '$' ? r.res.substr(1) : r.res, "");   // $backbuffer → backbuffer
				if (name.empty())
					name = (output ? "out" : "in") + std::to_string(i);
				std::string unique = name;
				for (int k = 2; taken.count(unique); ++k)
					unique = name + std::to_string(k);
				taken.insert(unique);
				r.name = unique;
			}
		};
		fill(pass.inputs, false);
		fill(pass.outputs, true);
	}
}

void EditorApp::registerLinkageMethods()
{
	Bridge& b = m_host.bridge();

	b.registerMethod("pass.linkage", [this](const Json& p, Bridge::Reply r) {
		const std::string pass = p.value("pass", std::string());
		m_render.postScene([r, pass](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
			r.ok(linkageJson(*scene, pass));
		});
	});

	b.registerMethod("pass.declare", [this](const Json& p, Bridge::Reply r) {
		const std::string pass  = p.value("pass", std::string());
		const std::string kind  = p.value("kind", std::string("input"));
		const size_t      index = p.value("index", 0u);
		m_render.postScene([this, r, pass, kind, index](GraphicsEngine&, ViewportRenderer* scene) {
			if (!scene)
				return r.error(BridgeError::Unavailable, "プロジェクトを開いていません");
			const PassInfo info = collect(*scene, pass);
			if (!info.pass || !info.asset)
				return r.error(BridgeError::InvalidParams, info.error.empty() ? "このパスにはシェーダーがありません" : info.error);
			const auto& refs = kind == "output" ? info.pass->outputs : info.pass->inputs;
			if (index >= refs.size())
				return r.error(BridgeError::InvalidParams, "その入力 / 出力はありません");
			const gfx::rg::PassResourceRef& ref = refs[index];
			// 書き足すステージ: Compute は CS、ほかは PS
			const std::string stageName = info.asset->compute ? "cs" : "ps";
			const auto st = std::find_if(info.stages.begin(), info.stages.end(), [&](const StageText& s) { return s.stage == stageName; });
			if (st == info.stages.end())
				return r.error(BridgeError::InvalidParams, "パイプラインに " + stageName + " のシェーダーがありません");
			std::string text = st->text;
			std::string name;
			int line = 0;
			// 変数名の元: 定義名、なければつないだリソースの名前、なければ in<番号> / out<番号>
			const std::string base = !ref.name.empty() ? ref.name : !ref.res.empty() ? ref.res : (kind == "output" ? "out" : "in") + std::to_string(index);
			const std::string from = ref.res.empty() ? "未接続" : ref.res;
			if (kind == "output" && ref.as == gfx::rg::Access::RTV)
			{
				if (std::any_of(info.targets.begin(), info.targets.end(), [&](const TargetDecl& t) { return t.index == ref.rtvIndex; }))
					return r.error(BridgeError::InvalidParams, "SV_Target" + std::to_string(ref.rtvIndex) + " はもう宣言しています");
				name = shaderIdentifierFor(base, text);
				const auto next = insertTargetMember(text, rtvTypeFor(scene->graph().desc(), ref), name, ref.rtvIndex, &line);
				if (!next)
					return r.error(BridgeError::InvalidParams,
					               "SV_Target のメンバーを持つ構造体がないので、自動では足せません。PS の戻り値を構造体にしてください (" + st->file + ")");
				text = *next;
			}
			else
			{
				if (!ref.bind.valid())
					return r.error(BridgeError::InvalidParams, "この入力 / 出力は bind (シェーダーの変数) を使いません");
				const auto slot = tableSlotRegister(info.asset->layout, static_cast<uint32_t>(ref.bind.param), ref.bind.offset);
				if (!slot)
					return r.error(BridgeError::InvalidParams, "bind がパイプラインのテーブルの外です");
				if (findDecl(info, slot->cls, slot->reg, slot->space))
					return r.error(BridgeError::InvalidParams, regName(slot->cls, slot->reg, slot->space) + " はもう宣言しています");
				name = shaderIdentifierFor(base, text);
				const std::string type = slot->cls == 'u' ? "RWTexture2D<float4>" : srvTypeFor(scene->graph().desc(), ref.res);
				const std::string reg  = std::string(1, slot->cls) + std::to_string(slot->reg) + (slot->space ? ", space" + std::to_string(slot->space) : "");
				text = insertResourceDecl(text, type + " " + name + " : register(" + reg + ");   // " + from + " (" + pass + " の" +
				                                (kind == "output" ? "出力" : "入力") + ")", &line);
			}
			std::string error;
			if (!gfx::rg::writeTextFileAtomic(scene->assetRoot() / gfx::rg::pathFromUtf8(st->file), text, &error))
				return r.error(BridgeError::Internal, "書けません: " + st->file + " " + error);
			log("info", "shader", st->file + ": " + name + " の宣言を書き足しました (" + std::to_string(line) + " 行目)");
			r.ok({ { "file", st->file }, { "name", name }, { "line", line } });
		});
	});
}
} // namespace studio
