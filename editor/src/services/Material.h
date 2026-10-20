#pragma once
// ============================================================
//  editor/src/services/Material.h
//
//  マテリアルのファイル (materials/*.material.json、M5-4) と、パラメーターを定数バッファに詰める処理。
//  GPU も webview も使わないので単体テストできる。
//
//    { "version": 1,
//      "passes":   { "Opaque": "pipelines/materials/standard.gbuffer.json" },   // マテリアルパス → パイプライン
//      "params":   { "baseColor": [1, 1, 1, 1], "roughness": 0.6 },             // cbuffer Material の変数の値
//      "textures": { "baseColorMap": "asset:<ID>" } }                          // テクスチャの変数 → アセット
//
//  ・マテリアルパスの名前は、レンダーグラフの drawList の layer と同じ。そのパスで、このマテリアルの物体を
//    パスのパイプラインの代わりにここのパイプラインで描く (出力のフォーマットはパスから)
//  ・パラメーターの欄はシェーダーのリフレクション (cbuffer Material) から作る。書いていない変数は 0
//  ・パイプラインのバインドの約束: ルートパラメーター 0 = 物体 (b0)、1 = フレーム (b1)、2 = マテリアル (b2)、
//    3 = テクスチャの表 (t0〜)。サンプラーはパイプラインの静的サンプラー
//  ・テクスチャの色空間は変数名で決める (Color / Albedo / Emissive を含めば sRGB、ほかはリニア)。
//    割り当てていないテクスチャは、名前に Normal を含めば平らな法線、ほかは白
// ============================================================

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

constexpr int k_MaterialVersion = 1;

// バインドの約束 (ルートパラメーターの番号)
constexpr uint32_t k_MaterialParamObject   = 0;
constexpr uint32_t k_MaterialParamFrame    = 1;
constexpr uint32_t k_MaterialParamMaterial = 2;
constexpr uint32_t k_MaterialParamTextures = 3;

struct MaterialDesc
{
	int                                version = k_MaterialVersion;
	std::map<std::string, std::string> passes;     // マテリアルパス → パイプラインのアセット
	Json                               params = Json::object();
	std::map<std::string, std::string> textures;   // 変数名 → "asset:<ID>"
};

struct MaterialDiagnostics
{
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
};

Json materialToJson(const MaterialDesc& m);
// 形が違えばエラー。知らないキーは警告
bool materialFromJson(const Json& json, MaterialDesc& out, MaterialDiagnostics& diag);

// ---- 定数バッファ (cbuffer Material) ----
struct CBufferVar
{
	enum class Kind { Float, Int, UInt, Bool };
	std::string name;
	uint32_t    offset   = 0;   // バイト
	uint32_t    size     = 0;   // バイト
	Kind        kind     = Kind::Float;
	uint32_t    rows     = 1;
	uint32_t    columns  = 1;   // float4 なら 4
	uint32_t    elements = 0;   // 配列の要素数 (0 = 配列でない)
};

struct CBufferLayout
{
	std::string             name;   // "Material"
	uint32_t                size = 0;
	std::vector<CBufferVar> vars;
};

// params の値を layout のとおりに詰める (足りない変数は 0)。数 / 配列 / 真偽値を受け付け、型や長さが違えば警告
std::vector<uint8_t> packMaterialParams(const CBufferLayout& layout, const Json& params, std::vector<std::string>* warnings = nullptr);

// テクスチャの変数名から、色空間 (sRGB) と、割り当てていないときの既定のテクスチャ
bool isColorTextureName(const std::string& name);
enum class DefaultTexture { White, FlatNormal };
DefaultTexture defaultTextureFor(const std::string& name);

// "asset:<ID>" の ID (小文字の 16 進 32 文字)。形が違えば空
std::string textureAssetId(const std::string& ref);
} // namespace studio
