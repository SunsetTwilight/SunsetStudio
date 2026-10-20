#pragma once
// ============================================================
//  editor/src/services/LightTypes.h
//
//  ライトの種類 (M5-5)。データで定義し、独自の種類も足せる。GPU も webview も使わないので単体テストできる。
//
//    lights/<種類>.light.json   パラメーターの名前 / 型 / 既定値
//      { "version": 1, "label": "ポイント", "shape": "point",
//        "params": [ { "name": "color", "type": "color", "default": [1, 1, 1] },
//                    { "name": "intensity", "type": "float", "default": 5, "min": 0 },
//                    { "name": "range", "type": "float", "default": 10, "min": 0 } ] }
//    lights/<種類>.light.hlsl   その種類の光の計算の関数
//      LightSample Light_<種類>(LightSurface s, LightBase b, LightParams_<種類> p)
//
//  エディターは全種類をまとめた shaders/generated/lights.hlsli を作る (generateLightsHlsli)。
//  Lighting シェーダーはそれを include し、evaluateLight(gLights[i], surface) を呼ぶだけ。
//
//  ・種類の名前はファイル名 (英字か _ で始まり、英数字と _)。番号は名前の順
//  ・パラメーターは 1 つのライトで 16 個の float まで (float4 × 4)。float4 の境目をまたがないように詰める
//  ・ライトの向きは物体のローカルの -Z (glTF と同じ)。位置は物体の位置
// ============================================================

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

constexpr int      k_LightTypeVersion  = 1;
constexpr uint32_t k_MaxLights         = 64;   // Lighting パスに渡すライトの数の上限
constexpr uint32_t k_LightParamFloats  = 16;   // 1 つのライトのパラメーターの float の数

struct LightParamDef
{
	enum class Kind { Float, Float2, Float3, Float4, Color };
	std::string name;
	Kind        kind  = Kind::Float;
	std::string label;               // UI の表示名 (空なら name)
	std::vector<float> defaults;     // 成分の数だけ
	float       min = -1e30f, max = 1e30f;
	uint32_t    offset = 0;          // params の float の位置
	uint32_t    components() const;
};

struct LightTypeDef
{
	std::string                id;      // ファイル名 ("point")
	std::string                label;
	std::string                shape;   // "directional" / "point" / "spot" / "" (印の形の手がかり)
	std::vector<LightParamDef> params;
};

struct LightTypeDiagnostics
{
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
};

bool isValidLightTypeId(const std::string& id);
// 定義を読み、パラメーターの位置を決める。形が違えばエラー
bool lightTypeFromJson(const std::string& id, const Json& json, LightTypeDef& out, LightTypeDiagnostics& diag);
// UI 用: { id, label, shape, params: [ { name, label, type, default, min?, max? } ] }
Json lightTypeToJson(const LightTypeDef& t);

// ライトの値 (名前 → 数 / 配列) を 16 個の float に詰める。書いていない / 形が違うものは既定値
std::vector<float> packLightParams(const LightTypeDef& t, const Json& values, std::vector<std::string>* warnings = nullptr);

// Lighting シェーダーが include する hlsli。types は名前の順に並べたもの (番号 = 並びの位置)。
// 関数のファイルは include する (パスは生成したファイルからの相対: "../../lights/<種類>.light.hlsl")
std::string generateLightsHlsli(const std::vector<LightTypeDef>& types);
} // namespace studio
