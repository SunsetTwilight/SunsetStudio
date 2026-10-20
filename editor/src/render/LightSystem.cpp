// ============================================================
//  editor/src/render/LightSystem.cpp
// ============================================================
#include "LightSystem.h"

#include "gfx/rendergraph/GraphAsset.h"

#include <algorithm>
#include <cstring>

namespace studio
{
namespace fs = std::filesystem;

namespace
{
// generateLightsHlsli の LightData / cbuffer Lights と合わせる
struct LightGpu
{
	float    position[4];
	float    direction[4];
	float    params[k_LightParamFloats];
	uint32_t info[4];
};
static_assert(sizeof(LightGpu) == 112, "cbuffer の配列の要素は 16 バイトの倍数");
struct LightsConstants
{
	uint32_t count[4];
	LightGpu lights[k_MaxLights];
};

std::string fileNameUtf8(const fs::path& p)
{
	const std::u8string u = p.filename().u8string();
	return std::string(u.begin(), u.end());
}

bool endsWith(const std::string& s, const std::string& tail)
{
	return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
} // namespace

fs::path LightSystem::generatedPath(const fs::path& root)
{
	return root / "shaders" / "generated" / "lights.hlsli";
}

bool LightSystem::init(GraphicsEngine& engine, const fs::path& root, std::string& err)
{
	m_root = root;
	m_cb   = engine.createBuffer({ sizeof(LightsConstants), BufferUsage::Uniform });
	if (!m_cb.valid())
		return err = "ライトの定数バッファを作れません", false;
	reloadTypes();
	m_dirty = true;
	return true;
}

void LightSystem::shutdown(GraphicsEngine& engine)
{
	if (m_cb.valid())
		engine.destroyBuffer(m_cb);
	m_cb = {};
	m_types.clear();
	m_stamps.clear();
	m_lights.clear();
	m_errors.clear();
	m_reported.clear();
}

void LightSystem::error(const std::string& text)
{
	if (m_reported.insert(text).second)
		m_errors.push_back(text);
}

std::vector<std::string> LightSystem::takeErrors()
{
	std::vector<std::string> out;
	out.swap(m_errors);
	return out;
}

const LightTypeDef* LightSystem::type(const std::string& id) const
{
	for (const LightTypeDef& t : m_types)
		if (t.id == id)
			return &t;
	return nullptr;
}

void LightSystem::reloadTypes()
{
	m_stamps.clear();
	m_types.clear();
	m_reported.clear();
	std::error_code ec;
	const fs::path dir = m_root / "lights";
	std::vector<std::pair<std::string, fs::path>> files;
	for (const auto& e : fs::directory_iterator(dir, ec))
	{
		if (!e.is_regular_file(ec))
			continue;
		const std::string name = fileNameUtf8(e.path());
		if (endsWith(name, ".light.json") || endsWith(name, ".light.hlsl"))
			m_stamps[name] = fs::last_write_time(e.path(), ec);
		if (endsWith(name, ".light.json"))
			files.emplace_back(name.substr(0, name.size() - std::string(".light.json").size()), e.path());
	}
	std::sort(files.begin(), files.end());
	for (const auto& [id, file] : files)
	{
		std::string text;
		Json json;
		LightTypeDiagnostics diag;
		LightTypeDef def;
		bool ok = gfx::rg::readTextFile(file, text);
		if (ok)
			try { json = Json::parse(text); } catch (const std::exception& e) { diag.errors.push_back(std::string("JSON: ") + e.what()); ok = false; }
		ok = ok && lightTypeFromJson(id, json, def, diag);
		for (const auto& w : diag.warnings)
			error("lights/" + id + ".light.json: " + w);
		if (!ok)
		{
			for (const auto& e : diag.errors)
				error("lights/" + id + ".light.json: " + e);
			continue;
		}
		if (!fs::exists(dir / (id + ".light.hlsl"), ec))
		{
			error("lights/" + id + ".light.hlsl がないので、種類 '" + id + "' は使いません (Light_" + id + " の関数を書いてください)");
			continue;
		}
		m_types.push_back(std::move(def));
	}

	// まとめた hlsli (中身が同じなら書かない: シェーダーを無駄に作り直さない)
	const std::string text = generateLightsHlsli(m_types);
	const fs::path out = generatedPath(m_root);
	std::string old;
	if (!gfx::rg::readTextFile(out, old) || old != text)
	{
		fs::create_directories(out.parent_path(), ec);
		std::string err;
		if (!gfx::rg::writeTextFileAtomic(out, text, &err))
			error("shaders/generated/lights.hlsli を書けません: " + err);
	}
	m_dirty = true;
}

bool LightSystem::poll()
{
	const auto now = std::chrono::steady_clock::now();
	if (now - m_lastPoll < std::chrono::milliseconds(500))
		return false;
	m_lastPoll = now;
	std::map<std::string, fs::file_time_type> stamps;
	std::error_code ec;
	for (const auto& e : fs::directory_iterator(m_root / "lights", ec))
	{
		if (!e.is_regular_file(ec))
			continue;
		const std::string name = fileNameUtf8(e.path());
		if (endsWith(name, ".light.json") || endsWith(name, ".light.hlsl"))
			stamps[name] = fs::last_write_time(e.path(), ec);
	}
	if (stamps == m_stamps)
		return false;
	reloadTypes();
	return true;
}

void LightSystem::setLights(std::vector<Light> lights)
{
	m_lights = std::move(lights);
	m_dirty  = true;
}

BufferHandle LightSystem::constants(GraphicsEngine& engine)
{
	if (!m_cb.valid() || !m_dirty)
		return m_cb;
	m_dirty = false;
	auto c = std::make_unique<LightsConstants>();
	std::memset(c.get(), 0, sizeof(LightsConstants));
	uint32_t n = 0;
	for (const Light& l : m_lights)
	{
		const auto it = std::find_if(m_types.begin(), m_types.end(), [&](const LightTypeDef& t) { return t.id == l.type; });
		if (it == m_types.end())
		{
			error("ライトの種類 '" + l.type + "' がありません (lights/" + l.type + ".light.json)");
			continue;
		}
		if (n >= k_MaxLights)
		{
			error("ライトは " + std::to_string(k_MaxLights) + " 個までです (それより後のライトは使いません)");
			break;
		}
		LightGpu& g = c->lights[n++];
		g.position[0]  = l.position.x;  g.position[1]  = l.position.y;  g.position[2]  = l.position.z;  g.position[3]  = 1;
		g.direction[0] = l.direction.x; g.direction[1] = l.direction.y; g.direction[2] = l.direction.z; g.direction[3] = 0;
		std::vector<std::string> warnings;
		const std::vector<float> p = packLightParams(*it, l.params, &warnings);
		for (const auto& w : warnings)
			error("ライト (" + l.type + "): " + w);
		std::copy_n(p.begin(), k_LightParamFloats, g.params);
		g.info[0] = static_cast<uint32_t>(it - m_types.begin());
		g.info[1] = l.mask;
	}
	c->count[0] = n;
	engine.updateBuffer(m_cb, c.get(), sizeof(LightsConstants));
	return m_cb;
}
} // namespace studio
