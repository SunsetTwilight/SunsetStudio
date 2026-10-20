// ============================================================
//  editor/src/services/Material.cpp
// ============================================================
#include "Material.h"

#include "ProjectAssets.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace studio
{
namespace
{
std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool stringMap(const Json& j, const std::string& at, std::map<std::string, std::string>& out, MaterialDiagnostics& diag)
{
	if (!j.is_object())
		return diag.errors.push_back(at + ": オブジェクトが必要です"), false;
	bool ok = true;
	for (const auto& [k, v] : j.items())
	{
		if (v.is_null())
			continue;   // 割り当てなし
		if (!v.is_string())
		{
			diag.errors.push_back(at + "." + k + ": 文字列が必要です");
			ok = false;
			continue;
		}
		out[k] = v.get<std::string>();
	}
	return ok;
}
} // namespace

Json materialToJson(const MaterialDesc& m)
{
	Json j = { { "version", m.version }, { "passes", Json::object() }, { "params", m.params.is_object() ? m.params : Json::object() },
	           { "textures", Json::object() } };
	for (const auto& [k, v] : m.passes)   j["passes"][k]   = v;
	for (const auto& [k, v] : m.textures) j["textures"][k] = v;
	return j;
}

bool materialFromJson(const Json& json, MaterialDesc& out, MaterialDiagnostics& diag)
{
	out = {};
	if (!json.is_object())
		return diag.errors.push_back("マテリアルは JSON のオブジェクトが必要です"), false;
	for (const auto& [k, v] : json.items())
	{
		if (k == "version")
		{
			if (!v.is_number_integer())
				diag.errors.push_back("version: 整数が必要です");
			else if ((out.version = v.get<int>()) > k_MaterialVersion)
				diag.errors.push_back("version: このエディターより新しい形式です (" + std::to_string(out.version) + ")");
		}
		else if (k == "passes")
		{
			stringMap(v, "passes", out.passes, diag);
			for (const auto& [pass, pipeline] : out.passes)
				if (pipeline.empty())
					diag.errors.push_back("passes." + pass + ": パイプラインのパスが空です");
		}
		else if (k == "params")
		{
			if (!v.is_object())
				diag.errors.push_back("params: オブジェクトが必要です");
			else
				out.params = v;
		}
		else if (k == "textures")
		{
			stringMap(v, "textures", out.textures, diag);
			for (const auto& [name, ref] : out.textures)
				if (textureAssetId(ref).empty())
					diag.errors.push_back("textures." + name + ": \"asset:<ID>\" の形が必要です (" + ref + ")");
		}
		else
			diag.warnings.push_back(k + ": 知らないキーなので無視します");
	}
	if (out.passes.empty())
		diag.warnings.push_back("passes: マテリアルパスがないので、どのパスでも描かれません");
	return diag.errors.empty();
}

std::vector<uint8_t> packMaterialParams(const CBufferLayout& layout, const Json& params, std::vector<std::string>* warnings)
{
	std::vector<uint8_t> out(layout.size, 0);
	auto warn = [&](const std::string& w) { if (warnings) warnings->push_back(w); };
	if (!params.is_object())
		return out;
	for (const auto& [name, value] : params.items())
	{
		const auto it = std::find_if(layout.vars.begin(), layout.vars.end(), [&](const CBufferVar& v) { return v.name == name; });
		if (it == layout.vars.end())
		{
			warn(name + ": シェーダーの cbuffer " + layout.name + " にない変数です");
			continue;
		}
		const CBufferVar& var = *it;
		if (var.rows != 1)
		{
			warn(name + ": 行列はまだ設定できません");
			continue;
		}
		// 値を数の並びにする
		std::vector<double> nums;
		auto push = [&](const Json& x) {
			if (x.is_boolean()) nums.push_back(x.get<bool>() ? 1.0 : 0.0);
			else if (x.is_number()) nums.push_back(x.get<double>());
			else return false;
			return true;
		};
		bool ok = true;
		if (value.is_array())
			for (const Json& x : value) ok = push(x) && ok;
		else
			ok = push(value);
		if (!ok)
		{
			warn(name + ": 数か、数の配列が必要です");
			continue;
		}
		const uint32_t count = std::max(var.elements, 1u);
		const uint32_t want  = var.columns * count;
		if (nums.size() != want)
			warn(name + ": 値の数が違います (" + std::to_string(nums.size()) + " 個。" + std::to_string(want) + " 個が必要)");
		// 配列の要素は 16 バイトごと (HLSL の cbuffer の詰め方)
		for (uint32_t e = 0; e < count; ++e)
			for (uint32_t c = 0; c < var.columns; ++c)
			{
				const size_t i = static_cast<size_t>(e) * var.columns + c;
				if (i >= nums.size())
					break;
				const uint32_t off = var.offset + e * 16 + c * 4;
				if (off + 4 > out.size())
					break;
				uint8_t* dst = out.data() + off;
				switch (var.kind)
				{
				case CBufferVar::Kind::Float: { const float v = static_cast<float>(nums[i]); std::memcpy(dst, &v, 4); break; }
				case CBufferVar::Kind::Int:   { const int32_t v = static_cast<int32_t>(nums[i]); std::memcpy(dst, &v, 4); break; }
				case CBufferVar::Kind::UInt:
				case CBufferVar::Kind::Bool:  { const uint32_t v = static_cast<uint32_t>(std::max(0.0, nums[i])); std::memcpy(dst, &v, 4); break; }
				}
			}
	}
	return out;
}

bool isColorTextureName(const std::string& name)
{
	const std::string n = lower(name);
	return n.find("color") != std::string::npos || n.find("albedo") != std::string::npos || n.find("emissive") != std::string::npos;
}

DefaultTexture defaultTextureFor(const std::string& name)
{
	return lower(name).find("normal") != std::string::npos ? DefaultTexture::FlatNormal : DefaultTexture::White;
}

std::string textureAssetId(const std::string& ref)
{
	return assetIdOfModel(ref).value_or(std::string());
}
} // namespace studio
