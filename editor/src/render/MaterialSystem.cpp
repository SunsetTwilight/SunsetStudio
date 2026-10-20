// ============================================================
//  editor/src/render/MaterialSystem.cpp
// ============================================================
#include "MaterialSystem.h"

#include "gfx/rendergraph/GraphAsset.h"

#include <algorithm>

namespace studio
{
namespace fs = std::filesystem;

namespace
{
const char* kindName(CBufferVar::Kind k)
{
	switch (k)
	{
	case CBufferVar::Kind::Int:  return "int";
	case CBufferVar::Kind::UInt: return "uint";
	case CBufferVar::Kind::Bool: return "bool";
	default:                     return "float";
	}
}

bool kindOf(GfxShaderVariableType t, CBufferVar::Kind& out)
{
	switch (t)
	{
	case GfxShaderVariableType::Float: out = CBufferVar::Kind::Float; return true;
	case GfxShaderVariableType::Int:   out = CBufferVar::Kind::Int;   return true;
	case GfxShaderVariableType::UInt:  out = CBufferVar::Kind::UInt;  return true;
	case GfxShaderVariableType::Bool:  out = CBufferVar::Kind::Bool;  return true;
	default:                           return false;
	}
}
} // namespace

void MaterialSystem::init(GraphicsEngine& engine, ShaderLibrary& shaders, gfx::rg::PipelineLibrary& pipelines, const fs::path& root)
{
	m_engine    = &engine;
	m_shaders   = &shaders;
	m_pipelines = &pipelines;
	m_root      = root;
	sunset::asset::Texture white;
	white.width = white.height = 1;
	white.rgba  = { 255, 255, 255, 255 };
	m_white = makeTexture(engine, white, false, "Material.White");
	sunset::asset::Texture flat = white;
	flat.rgba = { 128, 128, 255, 255 };
	m_flatNormal = makeTexture(engine, flat, false, "Material.FlatNormal");
}

void MaterialSystem::shutdown(GraphicsEngine& engine)
{
	for (auto& [key, inst] : m_instances)
		releasePasses(engine, inst);
	m_instances.clear();
	for (auto& [id, t] : m_textures)
	{
		if (t.srgb.valid())   engine.destroyTexture(t.srgb);
		if (t.linear.valid()) engine.destroyTexture(t.linear);
	}
	m_textures.clear();
	if (m_white.valid())      engine.destroyTexture(m_white);
	if (m_flatNormal.valid()) engine.destroyTexture(m_flatNormal);
	m_white = m_flatNormal = {};
	m_missing.clear();
	m_requested.clear();
	m_errors.clear();
	m_reported.clear();
	m_engine = nullptr;
}

void MaterialSystem::releasePasses(GraphicsEngine& engine, Instance& inst)
{
	for (auto& [pass, ps] : inst.passes)
		if (ps.cb.valid())
			engine.destroyBuffer(ps.cb);
	inst.passes.clear();
}

void MaterialSystem::error(const std::string& text)
{
	if (m_reported.insert(text).second)
		m_errors.push_back(text);
}

std::vector<std::string> MaterialSystem::takeErrors()
{
	std::vector<std::string> out;
	out.swap(m_errors);
	return out;
}

// ---- マテリアル ----
void MaterialSystem::setAuto(const std::string& key, const MaterialDesc& desc)
{
	Instance& inst = m_instances[key];
	inst.file   = false;
	inst.loaded = true;
	inst.ok     = true;
	inst.error.clear();
	inst.desc   = desc;
	for (auto& [pass, ps] : inst.passes)
		ps.dirty = true;
}

MaterialSystem::Instance& MaterialSystem::instance(const std::string& key)
{
	Instance& inst = m_instances[key];
	if (!inst.loaded && key.rfind("auto:", 0) != 0)
	{
		inst.file = true;
		load(inst, key);
	}
	return inst;
}

void MaterialSystem::load(Instance& inst, const std::string& path)
{
	inst.loaded = true;
	inst.ok     = false;
	inst.error.clear();
	const fs::path file = m_root / gfx::rg::pathFromUtf8(path);
	std::error_code ec;
	inst.time = fs::last_write_time(file, ec);
	std::string text;
	if (!gfx::rg::readTextFile(file, text))
	{
		inst.error = path + ": マテリアルのファイルを読めません";
		return error(inst.error);
	}
	Json json;
	try { json = Json::parse(text); }
	catch (const std::exception& e)
	{
		inst.error = path + ": JSON: " + e.what();
		return error(inst.error);
	}
	MaterialDiagnostics diag;
	MaterialDesc desc;
	if (!materialFromJson(json, desc, diag))
	{
		inst.error = path + ": " + (diag.errors.empty() ? std::string("読めません") : diag.errors.front());
		return error(inst.error);
	}
	for (const auto& w : diag.warnings)
		error(path + ": " + w);
	inst.desc = std::move(desc);
	inst.ok   = true;
	for (auto& [pass, ps] : inst.passes)
		ps.dirty = true;
}

void MaterialSystem::reload(const std::string& path)
{
	auto it = m_instances.find(path);
	if (it == m_instances.end() || !it->second.file)
		return;
	m_reported.clear();   // 直したあとの同じエラーも出す
	load(it->second, path);
}

void MaterialSystem::invalidateShaders()
{
	for (auto& [key, inst] : m_instances)
		for (auto& [pass, ps] : inst.passes)
			ps.reflected = false;
}

void MaterialSystem::poll()
{
	const auto now = std::chrono::steady_clock::now();
	if (now - m_lastPoll < std::chrono::milliseconds(500))
		return;
	m_lastPoll = now;
	for (auto& [key, inst] : m_instances)
	{
		if (!inst.file)
			continue;
		std::error_code ec;
		const auto t = fs::last_write_time(m_root / gfx::rg::pathFromUtf8(key), ec);
		if (!ec && t != inst.time)
		{
			m_reported.clear();
			load(inst, key);
		}
	}
}

bool MaterialSystem::hasPass(const std::string& key, const std::string& pass)
{
	Instance& inst = instance(key);
	return inst.ok && inst.desc.passes.count(pass) != 0;
}

void MaterialSystem::reflect(PassState& ps)
{
	ps.refl = {};
	const gfx::rg::PipelineAsset* asset = m_pipelines->asset(ps.pipeline);
	if (!asset)
		return void(ps.refl.error = "パイプライン '" + ps.pipeline + "' を読めません");
	ps.refl.material.name = "Material";
	auto add = [&](const gfx::rg::ShaderStageAsset& st, ShaderStage stage) -> bool {
		if (!st.used())
			return true;
		ShaderLibrary::Options opts;
		opts.entryPoint = st.entry;
		opts.profile    = st.profile;
		opts.defines    = st.defines;
		const ShaderHandle h = m_shaders->load(gfx::rg::pathFromUtf8(st.file), stage, opts);
		if (!h.valid())
			return ps.refl.error = "シェーダー '" + st.file + "': " + m_shaders->lastError(), false;
		auto r = std::make_unique<GfxShaderReflection>();
		if (!m_engine->getShaderReflection(h, *r))
			return ps.refl.error = "シェーダー '" + st.file + "' のリフレクションを取れません", false;
		for (uint32_t c = 0; c < r->cbufferCount && c < GFX_MAX_SHADER_CBUFFERS; ++c)
		{
			const GfxShaderCBuffer& cb = r->cbuffers[c];
			if (std::string(cb.name) != "Material")
				continue;
			ps.refl.material.size = std::max(ps.refl.material.size, cb.size);
			for (uint32_t v = cb.firstVariable; v < cb.firstVariable + cb.variableCount && v < r->variableCount; ++v)
			{
				const GfxShaderVariable& sv = r->variables[v];
				CBufferVar var;
				if (!kindOf(sv.variableType, var.kind) || sv.variableClass == GfxShaderVariableClass::Struct)
					continue;
				var.name     = sv.name;
				var.offset   = sv.offset;
				var.size     = sv.size;
				var.rows     = std::max(sv.rows, 1u);
				var.columns  = std::max(sv.columns, 1u);
				var.elements = sv.elements;
				auto& vars = ps.refl.material.vars;
				if (std::none_of(vars.begin(), vars.end(), [&](const CBufferVar& x) { return x.name == var.name; }))
					vars.push_back(var);
			}
		}
		for (uint32_t b = 0; b < r->bindingCount && b < GFX_MAX_SHADER_BINDINGS; ++b)
		{
			const GfxShaderBinding& bd = r->bindings[b];
			if (bd.type != GfxShaderInputType::Texture || bd.space != 0 || bd.dimension != GfxShaderResourceDimension::Texture2D)
				continue;
			auto& texs = ps.refl.textures;
			if (std::none_of(texs.begin(), texs.end(), [&](const TexBinding& x) { return x.name == bd.name; }))
				texs.push_back({ bd.name, bd.bindPoint });
			ps.refl.tableSize = std::max(ps.refl.tableSize, bd.bindPoint + 1);
		}
		return true;
	};
	if (!add(asset->vs, ShaderStage::Vertex) || !add(asset->ps, ShaderStage::Fragment))
		return;
	auto& vars = ps.refl.material.vars;
	std::sort(vars.begin(), vars.end(), [](const CBufferVar& a, const CBufferVar& b) { return a.offset < b.offset; });
	std::sort(ps.refl.textures.begin(), ps.refl.textures.end(), [](const TexBinding& a, const TexBinding& b) { return a.slot < b.slot; });
	ps.refl.ok = true;
}

bool MaterialSystem::prepare(GraphicsEngine& engine, const std::string& key, const std::string& pass,
                             const gfx::rg::PipelineTargets& targets, Bound& out, const std::string& fallbackPipeline)
{
	out = {};
	Instance& inst = instance(key);
	if (!inst.ok)
		return false;   // 読めない理由は load で記録した
	const auto it = inst.desc.passes.find(pass);
	const std::string& pipeline = it != inst.desc.passes.end() ? it->second : fallbackPipeline;
	if (pipeline.empty())
		return false;
	PassState& ps = inst.passes[pass];
	if (ps.pipeline != pipeline)
	{
		ps.pipeline  = pipeline;
		ps.reflected = false;
	}
	if (!ps.reflected)
	{
		reflect(ps);
		ps.reflected = true;
		ps.dirty     = true;
	}
	if (!ps.refl.ok)
		return error(key + " (" + pass + "): " + ps.refl.error), false;
	out.pipeline = m_pipelines->graphics(ps.pipeline, targets);
	if (!out.pipeline.valid())
		return error(key + " (" + pass + "): パイプライン '" + ps.pipeline + "' を作れません (理由はパイプラインのエラー)"), false;
	if (const GfxBindingLayoutDesc* layout = m_pipelines->bindingLayout(ps.pipeline))
		out.numParams = layout->numParameters;

	// 定数 (cbuffer Material)
	if (ps.refl.material.size > 0)
	{
		const uint32_t size = (ps.refl.material.size + 15u) & ~15u;
		if (ps.dirty || !ps.cb.valid())
		{
			std::vector<std::string> warnings;
			std::vector<uint8_t> bytes = packMaterialParams(ps.refl.material, inst.desc.params, &warnings);
			bytes.resize(size, 0);
			for (const auto& w : warnings)
				error(key + ": params." + w);
			if (ps.cb.valid() && ps.cbSize != size)
			{
				engine.destroyBuffer(ps.cb);
				ps.cb = {};
			}
			if (!ps.cb.valid())
			{
				ps.cb     = engine.createBuffer({ size, BufferUsage::Uniform });
				ps.cbSize = size;
			}
			if (ps.cb.valid())
				engine.updateBuffer(ps.cb, bytes.data(), bytes.size());
			ps.dirty = false;
		}
		out.constants = ps.cb;
	}

	// テクスチャの表 (割り当てがない / まだ届かないものは既定のテクスチャ)
	out.textures.assign(ps.refl.tableSize, gfxSRV(m_white));
	for (const TexBinding& tb : ps.refl.textures)
	{
		TextureHandle h;
		const auto t = inst.desc.textures.find(tb.name);
		if (t != inst.desc.textures.end())
			h = texture(engine, textureAssetId(t->second), isColorTextureName(tb.name));
		if (!h.valid())
			h = defaultTextureFor(tb.name) == DefaultTexture::FlatNormal ? m_flatNormal : m_white;
		out.textures[tb.slot] = gfxSRV(h);
	}
	return true;
}

Json MaterialSystem::describe(GraphicsEngine&, const std::string& key)
{
	Instance& inst = instance(key);
	Json j = { { "key", key }, { "ok", inst.ok }, { "auto", !inst.file } };
	if (!inst.error.empty())
		j["error"] = inst.error;
	if (!inst.ok)
		return j;
	j["material"] = materialToJson(inst.desc);
	Json passes = Json::object();
	for (const auto& [pass, pipeline] : inst.desc.passes)
	{
		PassState& ps = inst.passes[pass];
		if (ps.pipeline != pipeline || !ps.reflected)
		{
			ps.pipeline = pipeline;
			reflect(ps);
			ps.reflected = true;
			ps.dirty     = true;
		}
		Json p = { { "pipeline", pipeline }, { "ok", ps.refl.ok } };
		if (!ps.refl.ok)
			p["error"] = ps.refl.error;
		Json vars = Json::array();
		for (const CBufferVar& v : ps.refl.material.vars)
			vars.push_back({ { "name", v.name }, { "type", kindName(v.kind) }, { "rows", v.rows }, { "columns", v.columns },
			                 { "elements", v.elements }, { "offset", v.offset } });
		Json texs = Json::array();
		for (const TexBinding& t : ps.refl.textures)
			texs.push_back({ { "name", t.name }, { "slot", t.slot }, { "srgb", isColorTextureName(t.name) },
			                 { "default", defaultTextureFor(t.name) == DefaultTexture::FlatNormal ? "flatNormal" : "white" } });
		p["cbuffer"]  = std::move(vars);
		p["textures"] = std::move(texs);
		passes[pass]  = std::move(p);
	}
	j["passes"] = std::move(passes);
	return j;
}

// ---- テクスチャ ----
TextureHandle MaterialSystem::makeTexture(GraphicsEngine& engine, const sunset::asset::Texture& t, bool srgb, const std::string& name)
{
	if (t.width == 0 || t.height == 0 || t.rgba.size() < size_t(t.width) * t.height * 4)
		return {};
	GfxTextureResourceDesc d;
	d.width       = t.width;
	d.height      = t.height;
	d.mipLevels   = 1;
	d.format      = srgb ? GfxFormat::R8G8B8A8_Unorm_sRGB : GfxFormat::R8G8B8A8_Unorm;
	d.initialData = t.rgba.data();
	d.rowPitch    = t.width * 4;
	d.debugName   = name.c_str();
	return engine.createTexture(d);
}

TextureHandle MaterialSystem::texture(GraphicsEngine& engine, const std::string& id, bool srgb)
{
	if (id.empty())
		return {};
	auto it = m_textures.find(id);
	if (it == m_textures.end() || !it->second.cpu)
	{
		if (m_requested.insert(id).second)
			m_missing.insert(id);
		return {};
	}
	GpuTexture& g = it->second;
	TextureHandle& h = srgb ? g.srgb : g.linear;
	if (!h.valid())
		h = makeTexture(engine, *g.cpu, srgb, "Material." + id);
	return h;
}

void MaterialSystem::setTexture(GraphicsEngine& engine, const std::string& id, std::shared_ptr<const sunset::asset::Texture> texture)
{
	auto it = m_textures.find(id);
	if (it != m_textures.end())
	{
		// エンジンが GPU の使い終わりまで解放を遅らせる
		if (it->second.srgb.valid())   engine.destroyTexture(it->second.srgb);
		if (it->second.linear.valid()) engine.destroyTexture(it->second.linear);
		m_textures.erase(it);
	}
	m_missing.erase(id);
	m_requested.insert(id);   // 読めなかった (nullptr) ものも、何度も頼まない (更新のときに UI スレッドが送り直す)
	if (texture)
		m_textures[id].cpu = std::move(texture);
}

std::vector<std::string> MaterialSystem::takeMissingTextures()
{
	std::vector<std::string> out(m_missing.begin(), m_missing.end());
	m_missing.clear();
	return out;
}
} // namespace studio
