// ============================================================
//  editor/src/services/LightTypes.cpp
// ============================================================
#include "LightTypes.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace studio
{
namespace
{
const char* kindName(LightParamDef::Kind k)
{
	switch (k)
	{
	case LightParamDef::Kind::Float2: return "float2";
	case LightParamDef::Kind::Float3: return "float3";
	case LightParamDef::Kind::Float4: return "float4";
	case LightParamDef::Kind::Color:  return "color";
	default:                          return "float";
	}
}

const char* hlslType(LightParamDef::Kind k)
{
	switch (k)
	{
	case LightParamDef::Kind::Float2: return "float2";
	case LightParamDef::Kind::Float3:
	case LightParamDef::Kind::Color:  return "float3";
	case LightParamDef::Kind::Float4: return "float4";
	default:                          return "float";
	}
}

bool validName(const std::string& s)
{
	if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
		return false;
	return std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}
} // namespace

uint32_t LightParamDef::components() const
{
	switch (kind)
	{
	case Kind::Float2: return 2;
	case Kind::Float3:
	case Kind::Color:  return 3;
	case Kind::Float4: return 4;
	default:           return 1;
	}
}

bool isValidLightTypeId(const std::string& id)
{
	return validName(id) && id.size() <= 48;
}

bool lightTypeFromJson(const std::string& id, const Json& json, LightTypeDef& out, LightTypeDiagnostics& diag)
{
	out = {};
	out.id = id;
	if (!isValidLightTypeId(id))
		diag.errors.push_back("種類の名前 (ファイル名) は英字か _ で始まり、英数字と _ だけです: " + id);
	if (!json.is_object())
		return diag.errors.push_back("JSON のオブジェクトが必要です"), false;
	for (const auto& [k, v] : json.items())
	{
		if (k == "version")
		{
			if (!v.is_number_integer() || v.get<int>() > k_LightTypeVersion)
				diag.errors.push_back("version: このエディターが知らない形式です");
		}
		else if (k == "label")
		{
			if (v.is_string()) out.label = v.get<std::string>();
			else diag.errors.push_back("label: 文字列が必要です");
		}
		else if (k == "shape")
		{
			if (v.is_string()) out.shape = v.get<std::string>();
			else diag.errors.push_back("shape: 文字列が必要です");
		}
		else if (k == "params")
		{
			if (!v.is_array()) { diag.errors.push_back("params: 配列が必要です"); continue; }
			for (size_t i = 0; i < v.size(); ++i)
			{
				const Json& p = v[i];
				const std::string at = "params[" + std::to_string(i) + "]";
				if (!p.is_object()) { diag.errors.push_back(at + ": オブジェクトが必要です"); continue; }
				LightParamDef d;
				d.name = p.value("name", std::string());
				if (!validName(d.name)) { diag.errors.push_back(at + ".name: 英字か _ で始まり、英数字と _ だけです (" + d.name + ")"); continue; }
				if (std::any_of(out.params.begin(), out.params.end(), [&](const LightParamDef& x) { return x.name == d.name; }))
				{
					diag.errors.push_back(at + ".name: 名前が重なっています (" + d.name + ")");
					continue;
				}
				const std::string type = p.value("type", std::string("float"));
				if (type == "float") d.kind = LightParamDef::Kind::Float;
				else if (type == "float2") d.kind = LightParamDef::Kind::Float2;
				else if (type == "float3") d.kind = LightParamDef::Kind::Float3;
				else if (type == "float4") d.kind = LightParamDef::Kind::Float4;
				else if (type == "color") d.kind = LightParamDef::Kind::Color;
				else { diag.errors.push_back(at + ".type: float / float2 / float3 / float4 / color のどれかです (" + type + ")"); continue; }
				d.label = p.value("label", std::string());
				if (p.contains("min") && p["min"].is_number()) d.min = p["min"].get<float>();
				if (p.contains("max") && p["max"].is_number()) d.max = p["max"].get<float>();
				d.defaults.assign(d.components(), d.kind == LightParamDef::Kind::Color ? 1.0f : 0.0f);
				if (p.contains("default"))
				{
					const Json& def = p["default"];
					if (def.is_number() && d.components() == 1)
						d.defaults[0] = def.get<float>();
					else if (def.is_array() && def.size() == d.components() && std::all_of(def.begin(), def.end(), [](const Json& x) { return x.is_number(); }))
						for (size_t c = 0; c < def.size(); ++c) d.defaults[c] = def[c].get<float>();
					else
						diag.errors.push_back(at + ".default: " + std::to_string(d.components()) + " 個の数が必要です");
				}
				for (const auto& [pk, pv] : p.items())
					if (pk != "name" && pk != "type" && pk != "label" && pk != "default" && pk != "min" && pk != "max")
						diag.warnings.push_back(at + "." + pk + ": 知らないキーなので無視します");
				out.params.push_back(std::move(d));
			}
		}
		else
			diag.warnings.push_back(k + ": 知らないキーなので無視します");
	}
	// 位置を決める (float4 の境目をまたがない)
	uint32_t cursor = 0;
	for (LightParamDef& d : out.params)
	{
		const uint32_t n = d.components();
		if ((cursor % 4) + n > 4)
			cursor = (cursor + 3) / 4 * 4;
		d.offset = cursor;
		cursor += n;
	}
	if (cursor > k_LightParamFloats)
		diag.errors.push_back("params: 多すぎます (float " + std::to_string(cursor) + " 個分。" + std::to_string(k_LightParamFloats) + " 個まで)");
	if (out.label.empty())
		out.label = id;
	return diag.errors.empty();
}

Json lightTypeToJson(const LightTypeDef& t)
{
	Json params = Json::array();
	for (const LightParamDef& d : t.params)
	{
		Json p = { { "name", d.name }, { "label", d.label.empty() ? d.name : d.label }, { "type", kindName(d.kind) } };
		p["default"] = d.components() == 1 ? Json(d.defaults[0]) : Json(d.defaults);
		if (d.min > -1e29f) p["min"] = d.min;
		if (d.max < 1e29f)  p["max"] = d.max;
		params.push_back(std::move(p));
	}
	return { { "id", t.id }, { "label", t.label }, { "shape", t.shape }, { "params", std::move(params) } };
}

std::vector<float> packLightParams(const LightTypeDef& t, const Json& values, std::vector<std::string>* warnings)
{
	std::vector<float> out(k_LightParamFloats, 0.0f);
	for (const LightParamDef& d : t.params)
	{
		std::vector<float> v = d.defaults;
		if (values.is_object() && values.contains(d.name))
		{
			const Json& x = values[d.name];
			if (x.is_number() && d.components() == 1)
				v[0] = x.get<float>();
			else if (x.is_array() && x.size() == d.components() && std::all_of(x.begin(), x.end(), [](const Json& e) { return e.is_number(); }))
				for (size_t c = 0; c < x.size(); ++c) v[c] = x[c].get<float>();
			else if (warnings)
				warnings->push_back(d.name + ": " + std::to_string(d.components()) + " 個の数が必要です (既定値を使います)");
		}
		for (uint32_t c = 0; c < d.components() && d.offset + c < out.size(); ++c)
			out[d.offset + c] = std::clamp(v[c], d.min, d.max);
	}
	return out;
}

std::string generateLightsHlsli(const std::vector<LightTypeDef>& types)
{
	static const char* comp = "xyzw";
	std::ostringstream o;
	o << "// ============================================================\n"
	     "//  自動で作るファイル (lights/*.light.json と *.light.hlsl から。SunsetEditor が書き直す)。直接編集しないこと\n"
	     "//\n"
	     "//  Lighting シェーダーは、これを include して evaluateLight(gLights[i], surface) を呼ぶ。\n"
	     "//  LightData::info.x = 種類の番号、info.y = ライトリンクのマスク (0xFFFFFFFF = すべての物体)\n"
	     "// ============================================================\n"
	     "#ifndef GENERATED_LIGHTS_HLSLI\n#define GENERATED_LIGHTS_HLSLI\n\n"
	  << "#define MAX_LIGHTS " << k_MaxLights << "\n\n"
	  << "struct LightData\n{\n\tfloat4 position;    // xyz = ワールドの位置\n\tfloat4 direction;   // xyz = ライトの前 (ローカルの -Z をワールドへ、正規化)\n"
	     "\tfloat4 params[4];   // 種類ごとのパラメーター (unpackLight_<種類> で取り出す)\n\tuint4  info;\n};\n\n"
	     "cbuffer Lights : register(b2)\n{\n\tuint4     gLightCount;   // x = ライトの数\n\tLightData gLights[MAX_LIGHTS];\n};\n\n"
	     "struct LightSurface\n{\n\tfloat3 position;   // ワールド\n\tfloat3 normal;     // 正規化\n\tfloat3 view;       // 表面からカメラへ (正規化)\n};\n\n"
	     "struct LightBase\n{\n\tfloat3 position;\n\tfloat3 direction;\n};\n\n"
	     "struct LightSample\n{\n\tfloat3 direction;   // 表面から光へ (正規化)\n\tfloat3 radiance;    // 届く光 (リニアの色 × 強さ)\n};\n\n"
	     "static const LightSample kNoLight = { float3(0, 1, 0), float3(0, 0, 0) };\n\n";
	for (size_t i = 0; i < types.size(); ++i)
	{
		const LightTypeDef& t = types[i];
		o << "// ---- " << t.id << " (" << t.label << ") ----\n";
		o << "#define LIGHT_TYPE_" << t.id << " " << i << "\n";
		o << "struct LightParams_" << t.id << "\n{\n";
		for (const LightParamDef& d : t.params)
			o << "\t" << hlslType(d.kind) << " " << d.name << ";\n";
		if (t.params.empty())
			o << "\tfloat _unused;\n";
		o << "};\n";
		o << "LightParams_" << t.id << " unpackLight_" << t.id << "(LightData d)\n{\n\tLightParams_" << t.id << " p;\n";
		if (t.params.empty())
			o << "\tp._unused = 0;\n";
		for (const LightParamDef& d : t.params)
		{
			o << "\tp." << d.name << " = d.params[" << d.offset / 4 << "].";
			for (uint32_t c = 0; c < d.components(); ++c)
				o << comp[(d.offset % 4) + c];
			o << ";\n";
		}
		o << "\treturn p;\n}\n";
		o << "#include \"../../lights/" << t.id << ".light.hlsl\"\n\n";
	}
	o << "LightSample evaluateLight(LightData d, LightSurface s)\n{\n\tLightBase b;\n\tb.position  = d.position.xyz;\n\tb.direction = d.direction.xyz;\n"
	     "\tswitch (d.info.x)\n\t{\n";
	for (const LightTypeDef& t : types)
		o << "\tcase LIGHT_TYPE_" << t.id << ": return Light_" << t.id << "(s, b, unpackLight_" << t.id << "(d));\n";
	o << "\tdefault: return kNoLight;\n\t}\n}\n\n#endif\n";
	return o.str();
}
} // namespace studio
