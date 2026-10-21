// ============================================================
//  editor/src/render/NodeResources.cpp
// ============================================================
#include "NodeResources.h"

#include "gfx/rendergraph/GraphAsset.h"

#include <algorithm>

namespace studio
{
namespace
{
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

void NodeResources::init(GraphicsEngine& engine, ShaderLibrary& shaders, gfx::rg::PipelineLibrary& pipelines)
{
	m_engine    = &engine;
	m_shaders   = &shaders;
	m_pipelines = &pipelines;
	sunset::asset::Texture white;
	white.width = white.height = 1;
	white.rgba  = { 255, 255, 255, 255 };
	m_white = makeTexture(engine, white, false, "NodeResources.White");
	sunset::asset::Texture flat = white;
	flat.rgba = { 128, 128, 255, 255 };
	m_flatNormal = makeTexture(engine, flat, false, "NodeResources.FlatNormal");
}

void NodeResources::shutdown(GraphicsEngine& engine)
{
	m_interfaces.clear();
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

void NodeResources::error(const std::string& text)
{
	if (m_reported.insert(text).second)
		m_errors.push_back(text);
}

std::vector<std::string> NodeResources::takeErrors()
{
	std::vector<std::string> out;
	out.swap(m_errors);
	return out;
}

const NodeResources::Interface& NodeResources::interfaceOf(const gfx::rg::PassDesc& pass)
{
	// パイプラインと、パスがつないでいるルートパラメーターの番号で覚える
	std::string key = pass.pipeline + "#";
	auto addKey = [&](int32_t p) { if (p >= 0) key += std::to_string(p) + ","; };
	for (const auto& c : pass.constants) addKey(c.bind.param);
	for (const auto& r : pass.inputs)    addKey(r.bind.param);
	for (const auto& r : pass.outputs)   addKey(r.bind.param);
	auto it = m_interfaces.find(key);
	if (it == m_interfaces.end())
	{
		it = m_interfaces.emplace(key, build(pass)).first;
		if (!it->second.ok)
			error("描画ノード " + pass.name + ": " + it->second.error);
	}
	return it->second;
}

NodeResources::Interface NodeResources::build(const gfx::rg::PassDesc& pass)
{
	Interface itf;
	const gfx::rg::PipelineAsset* asset = m_pipelines->asset(pass.pipeline);
	if (!asset)
		return itf.error = "パイプライン '" + pass.pipeline + "' を読めません", itf;
	const GfxBindingLayoutDesc& layout = asset->layout;

	// シェーダーのリフレクション (VS と PS の cbuffer とテクスチャ)
	std::vector<std::unique_ptr<GfxShaderReflection>> refls;
	auto reflect = [&](const gfx::rg::ShaderStageAsset& st, ShaderStage stage) -> bool {
		if (!st.used())
			return true;
		auto r = reflectStage(st, stage, itf.error);
		if (!r)
			return false;
		refls.push_back(std::move(r));
		return true;
	};
	if (!reflect(asset->vs, ShaderStage::Vertex) || !reflect(asset->ps, ShaderStage::Fragment))
		return itf;

	std::vector<bool> bound(layout.numParameters, false);
	auto mark = [&](int32_t p) { if (p >= 0 && p < static_cast<int32_t>(bound.size())) bound[static_cast<size_t>(p)] = true; };
	for (const auto& c : pass.constants) mark(c.bind.param);
	for (const auto& r : pass.inputs)    mark(r.bind.param);
	for (const auto& r : pass.outputs)   mark(r.bind.param);

	for (uint32_t i = 0; i < layout.numParameters; ++i)
	{
		if (bound[i])
			continue;
		const GfxRootParameter& rp = layout.parameters[i];
		if (rp.parameterType == GfxRootParameterType::CBV)
		{
			Interface::CBuffer cb;
			cb.param        = i;
			cb.layout.reg   = rp.shaderRegister;
			cb.layout.size  = 16;   // シェーダーが使っていなくても、空の定数バッファをつなぐ
			for (const auto& r : refls)
				for (uint32_t c = 0; c < r->cbufferCount && c < GFX_MAX_SHADER_CBUFFERS; ++c)
				{
					const GfxShaderCBuffer& sc = r->cbuffers[c];
					if (sc.bindPoint != rp.shaderRegister || sc.space != rp.registerSpace)
						continue;
					cb.layout.name = sc.name;
					cb.layout.size = std::max(cb.layout.size, (sc.size + 15u) & ~15u);
					for (uint32_t v = sc.firstVariable; v < sc.firstVariable + sc.variableCount && v < r->variableCount; ++v)
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
						auto& vars = cb.layout.vars;
						if (std::none_of(vars.begin(), vars.end(), [&](const CBufferVar& x) { return x.name == var.name; }))
							vars.push_back(var);
					}
				}
			std::sort(cb.layout.vars.begin(), cb.layout.vars.end(), [](const CBufferVar& a, const CBufferVar& b) { return a.offset < b.offset; });
			itf.cbuffers.push_back(std::move(cb));
		}
		else if (rp.parameterType == GfxRootParameterType::DescriptorTable)
		{
			Interface::Table table;
			table.param = i;
			// レンジ (SRV だけ) を並べて、t レジスタ → 表の位置
			struct Range { uint32_t base, count, space, start; };
			std::vector<Range> ranges;
			uint32_t at = 0;
			bool ok = rp.numRanges > 0;
			for (uint32_t k = rp.firstRange; ok && k < rp.firstRange + rp.numRanges && k < layout.numRanges; ++k)
			{
				const GfxDescriptorRange& dr = layout.ranges[k];
				if (dr.rangeType != GfxDescriptorRangeType::SRV || dr.numDescriptors == 0xFFFFFFFFu)
				{
					ok = false;
					break;
				}
				const uint32_t start = dr.offsetInDescriptorsFromTableStart == GFX_DESCRIPTOR_RANGE_OFFSET_APPEND ? at : dr.offsetInDescriptorsFromTableStart;
				ranges.push_back({ dr.baseShaderRegister, dr.numDescriptors, dr.registerSpace, start });
				at = start + dr.numDescriptors;
			}
			if (!ok)
				return itf.error = "パイプライン '" + pass.pipeline + "' のルートパラメーター " + std::to_string(i) +
				                   " (SRV 以外か上限なしのテーブル) は、物体ごとにはまだつなげません", itf;
			table.size = at;
			for (const auto& r : refls)
				for (uint32_t b = 0; b < r->bindingCount && b < GFX_MAX_SHADER_BINDINGS; ++b)
				{
					const GfxShaderBinding& bd = r->bindings[b];
					if (bd.type != GfxShaderInputType::Texture || bd.dimension != GfxShaderResourceDimension::Texture2D)
						continue;
					for (const Range& rg : ranges)
						if (bd.space == rg.space && bd.bindPoint >= rg.base && bd.bindPoint < rg.base + rg.count)
						{
							const uint32_t slot = rg.start + (bd.bindPoint - rg.base);
							auto& texs = table.textures;
							if (std::none_of(texs.begin(), texs.end(), [&](const Texture& x) { return x.name == bd.name; }))
								texs.push_back({ bd.name, slot });
						}
				}
			std::sort(table.textures.begin(), table.textures.end(), [](const Texture& a, const Texture& b) { return a.slot < b.slot; });
			itf.tables.push_back(std::move(table));
		}
		else
			return itf.error = "パイプライン '" + pass.pipeline + "' のルートパラメーター " + std::to_string(i) +
			                   " (ルート定数 / ルートの SRV・UAV) は、物体ごとにはまだつなげません", itf;
	}
	itf.ok = true;
	return itf;
}

std::unique_ptr<GfxShaderReflection> NodeResources::reflectStage(const gfx::rg::ShaderStageAsset& st, ShaderStage stage, std::string& error)
{
	ShaderLibrary::Options opts;
	opts.entryPoint = st.entry;
	opts.profile    = st.profile;
	opts.defines    = st.defines;
	const ShaderHandle h = m_shaders->load(gfx::rg::pathFromUtf8(st.file), stage, opts);
	if (!h.valid())
		return error = "シェーダー '" + st.file + "': " + m_shaders->lastError(), nullptr;
	auto r = std::make_unique<GfxShaderReflection>();
	if (!m_engine->getShaderReflection(h, *r))
		return error = "シェーダー '" + st.file + "' のリフレクションを取れません", nullptr;
	return r;
}

void NodeResources::prepare(GraphicsEngine& engine, const Interface& itf, const Json& values, uint64_t version, Cache& cache)
{
	const uint64_t want = version * 1000003ull + m_textureVersion;   // テクスチャが届いたら作り直す
	if (!stale(itf, cache, version))
		return;
	if (cache.buffers.size() != itf.cbuffers.size())
	{
		release(engine, cache);
		cache.buffers.resize(itf.cbuffers.size());
	}
	cache.generation = m_generation;
	cache.version    = want;
	cache.missing    = false;
	for (size_t i = 0; i < itf.cbuffers.size(); ++i)
	{
		const CBufferLayout& l = itf.cbuffers[i].layout;
		std::vector<uint8_t> bytes = packCBuffer(l, values);
		bytes.resize(l.size, 0);
		BufferHandle& b = cache.buffers[i];
		if (!b.valid())
			b = engine.createBuffer({ l.size, BufferUsage::Uniform });
		if (b.valid())
			engine.updateBuffer(b, bytes.data(), bytes.size());
	}
	cache.tables.assign(itf.tables.size(), {});
	for (size_t i = 0; i < itf.tables.size(); ++i)
	{
		const Interface::Table& t = itf.tables[i];
		auto& out = cache.tables[i];
		out.assign(t.size, gfxSRV(m_white));
		for (const Texture& tx : t.textures)
		{
			TextureHandle h;
			const auto v = values.find(tx.name);
			if (v != values.end() && v->is_string())
				h = texture(engine, textureAssetId(v->get<std::string>()), isColorTextureName(tx.name), cache.missing);
			if (!h.valid())
				h = defaultTextureFor(tx.name) == DefaultTexture::FlatNormal ? m_flatNormal : m_white;
			if (tx.slot < out.size())
				out[tx.slot] = gfxSRV(h);
		}
	}
}

void NodeResources::release(GraphicsEngine& engine, Cache& cache)
{
	for (BufferHandle& b : cache.buffers)
		if (b.valid())
			engine.destroyBuffer(b);   // エンジンが GPU の使い終わりまで解放を遅らせる
	cache = {};
}

void NodeResources::bind(ICommandBuffer* cb, const Interface& itf, const Cache& cache)
{
	for (size_t i = 0; i < itf.cbuffers.size() && i < cache.buffers.size(); ++i)
		if (cache.buffers[i].valid())
			cb->setRootDescriptor(itf.cbuffers[i].param, cache.buffers[i], 0);
	for (size_t i = 0; i < itf.tables.size() && i < cache.tables.size(); ++i)
		if (!cache.tables[i].empty())
			cb->setDescriptorTable(itf.tables[i].param, cache.tables[i].data(), static_cast<uint32_t>(cache.tables[i].size()));
}

Json NodeResources::describe(const Interface& itf)
{
	Json j = { { "ok", itf.ok } };
	if (!itf.error.empty())
		j["error"] = itf.error;
	Json cbs = Json::array();
	for (const auto& c : itf.cbuffers)
	{
		Json vars = Json::array();
		for (const CBufferVar& v : c.layout.vars)
			vars.push_back({ { "name", v.name }, { "type", cbufferKindName(v.kind) }, { "rows", v.rows }, { "columns", v.columns },
			                 { "elements", v.elements } });
		cbs.push_back({ { "name", c.layout.name }, { "register", c.layout.reg }, { "vars", std::move(vars) } });
	}
	Json texs = Json::array();
	for (const auto& t : itf.tables)
		for (const Texture& tx : t.textures)
			texs.push_back({ { "name", tx.name }, { "srgb", isColorTextureName(tx.name) },
			                 { "default", defaultTextureFor(tx.name) == DefaultTexture::FlatNormal ? "flatNormal" : "white" } });
	j["cbuffers"] = std::move(cbs);
	j["textures"] = std::move(texs);
	return j;
}

// ---- テクスチャ ----
TextureHandle NodeResources::makeTexture(GraphicsEngine& engine, const sunset::asset::Texture& t, bool srgb, const std::string& name)
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

TextureHandle NodeResources::texture(GraphicsEngine& engine, const std::string& id, bool srgb, bool& missing)
{
	if (id.empty())
		return {};
	auto it = m_textures.find(id);
	if (it == m_textures.end() || !it->second.cpu)
	{
		if (m_requested.insert(id).second)
		{
			m_missing.insert(id);
			missing = true;
		}
		return {};
	}
	GpuTexture& g = it->second;
	TextureHandle& h = srgb ? g.srgb : g.linear;
	if (!h.valid())
		h = makeTexture(engine, *g.cpu, srgb, "NodeResources." + id);
	return h;
}

void NodeResources::setTexture(GraphicsEngine& engine, const std::string& id, std::shared_ptr<const sunset::asset::Texture> texture)
{
	auto it = m_textures.find(id);
	if (it != m_textures.end())
	{
		if (it->second.srgb.valid())   engine.destroyTexture(it->second.srgb);
		if (it->second.linear.valid()) engine.destroyTexture(it->second.linear);
		m_textures.erase(it);
	}
	m_missing.erase(id);
	m_requested.insert(id);   // 読めなかった (nullptr) ものも、何度も頼まない (更新のときに UI スレッドが送り直す)
	if (texture)
		m_textures[id].cpu = std::move(texture);
	++m_textureVersion;      // 使っている描画の表を作り直す
}

std::vector<std::string> NodeResources::takeMissingTextures()
{
	std::vector<std::string> out(m_missing.begin(), m_missing.end());
	m_missing.clear();
	return out;
}
} // namespace studio
