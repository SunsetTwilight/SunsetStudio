// ============================================================
//  editor/src/services/NodeParams.cpp
// ============================================================
#include "NodeParams.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace studio
{
namespace
{
std::string lower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// float を書くとき 0.949999988 のようにならないよう、6 桁に丸めて double で持つ
double r6(float v)
{
	return std::round(static_cast<double>(v) * 1e6) / 1e6;
}
} // namespace

const char* cbufferKindName(CBufferVar::Kind k)
{
	switch (k)
	{
	case CBufferVar::Kind::Int:  return "int";
	case CBufferVar::Kind::UInt: return "uint";
	case CBufferVar::Kind::Bool: return "bool";
	default:                     return "float";
	}
}

std::vector<uint8_t> packCBuffer(const CBufferLayout& layout, const Json& values, std::vector<std::string>* warnings)
{
	std::vector<uint8_t> out(layout.size, 0);
	auto warn = [&](const std::string& w) { if (warnings) warnings->push_back(w); };
	if (!values.is_object())
		return out;
	for (const CBufferVar& var : layout.vars)
	{
		const auto it = values.find(var.name);
		if (it == values.end())
		{
			// 書いていない変数は 0。ただし名前に Color を含む float の変数は 1 (黒にならないように)
			if (var.kind == CBufferVar::Kind::Float && var.rows == 1 && lower(var.name).find("color") != std::string::npos)
				for (uint32_t e = 0; e < std::max(var.elements, 1u); ++e)
					for (uint32_t c = 0; c < var.columns; ++c)
					{
						const uint32_t off = var.offset + e * 16 + c * 4;
						const float one = 1.0f;
						if (off + 4 <= out.size())
							std::memcpy(out.data() + off, &one, 4);
					}
			continue;
		}
		const Json& value = *it;
		if (var.rows != 1)
		{
			warn(var.name + ": 行列はまだ設定できません");
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
			warn(var.name + ": 数か、数の配列が必要です");
			continue;
		}
		const uint32_t count = std::max(var.elements, 1u);
		const uint32_t want  = var.columns * count;
		// 取り込んだ値の float4 を float3 の変数に入れるなど、長いものは先頭を使う (短いものは警告)
		if (nums.size() < want)
			warn(var.name + ": 値の数が足りません (" + std::to_string(nums.size()) + " 個。" + std::to_string(want) + " 個が必要)");
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
	return n.find("color") != std::string::npos || n.find("albedo") != std::string::npos || n.find("emissive") != std::string::npos ||
	       n.find("diffuse") != std::string::npos || n == "maintex";
}

DefaultTexture defaultTextureFor(const std::string& name)
{
	return lower(name).find("normal") != std::string::npos ? DefaultTexture::FlatNormal : DefaultTexture::White;
}

std::string textureAssetId(const std::string& ref)
{
	if (ref.rfind("asset:", 0) != 0 || ref.size() != 6 + 32)
		return {};
	const std::string id = ref.substr(6);
	if (!std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
		return {};
	return id;
}

Json importedValues(const ImportedSlot& s)
{
	const Json color = { r6(s.baseColor[0]), r6(s.baseColor[1]), r6(s.baseColor[2]), r6(s.baseColor[3]) };
	Json j = Json::object();
	for (const char* n : { "baseColor", "albedo", "albedoColor", "diffuseColor" })
		j[n] = color;
	j["metallic"]    = r6(s.metallic);
	j["roughness"]   = r6(s.roughness);
	j["alphaCutoff"] = r6(s.alphaCutoff);
	auto tex = [&](std::initializer_list<const char*> names, const std::string& id) {
		if (id.empty())
			return;
		for (const char* n : names)
			j[n] = "asset:" + id;
	};
	tex({ "baseColorMap", "albedoMap", "diffuseMap", "mainTex", "colorMap" }, s.baseColorTexture);
	tex({ "normalMap", "bumpMap" }, s.normalTexture);
	tex({ "metallicRoughnessMap" }, s.metallicRoughnessTexture);
	return j;
}

Json mergeNodeParams(const Json& imported, const Json& object, const Json& slot)
{
	Json out = imported.is_object() ? imported : Json::object();
	for (const Json* layer : { &object, &slot })
		if (layer->is_object())
			for (const auto& [k, v] : layer->items())
			{
				if (v.is_null())
					out.erase(k);   // null は「取り込んだ値も使わない」
				else
					out[k] = v;
			}
	return out;
}
} // namespace studio
