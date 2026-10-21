#pragma once
// ============================================================
//  editor/src/services/NodeParams.h
//
//  描画ノードのリソースを物体ごとに設定する (2026-10-22)。
//  描画ノード (drawList のパス) のパイプラインのシェーダーが使う cbuffer (グラフがつながないもの) とテクスチャ (t) を、
//  「物体ごとに設定するリソース」とする。値はシーンの物体に、描画ノードの layer ごとに書く:
//    "nodeParams":     { "<layer>": { "<変数>": 値, "<テクスチャ>": "asset:<ID>" } }
//    "slotNodeParams": { "<スロット>": { "<layer>": { … } } }   (スロットごとの上書き)
//  決まる順: 取り込んだ値 (モデルのスロット。名前で合わせる) → 物体 → スロット。
//  ・値は数 / 数の配列 / 真偽値。テクスチャは "asset:<ID>" (ライブラリのテクスチャ)
//  ・テクスチャの色空間は変数名で決める (Color / Albedo / Emissive を含めば sRGB)。
//    設定していないテクスチャは、名前に Normal を含めば平らな法線、ほかは白
//  GPU も webview も使わない (tests/test_node_params.cpp)。
// ============================================================

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

// ---- 定数バッファ (リフレクションの cbuffer) ----
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
	std::string             name;   // cbuffer の名前
	uint32_t                reg  = 0;   // b レジスタ
	uint32_t                size = 0;
	std::vector<CBufferVar> vars;
};

const char* cbufferKindName(CBufferVar::Kind k);

// values の値を layout のとおりに詰める (書いていない変数は 0、名前に Color を含む float は 1。
// ほかの cbuffer の変数とテクスチャは無視する)。
// 数 / 配列 / 真偽値を受け付け、型や長さが違えば警告
std::vector<uint8_t> packCBuffer(const CBufferLayout& layout, const Json& values, std::vector<std::string>* warnings = nullptr);

// テクスチャの変数名から、色空間 (sRGB) と、設定していないときの既定のテクスチャ
bool isColorTextureName(const std::string& name);
enum class DefaultTexture { White, FlatNormal };
DefaultTexture defaultTextureFor(const std::string& name);
// "asset:<ID>" の ID (小文字の 16 進 32 文字)。形が違えば空
std::string textureAssetId(const std::string& ref);

// ---- 取り込んだ値 (モデルのスロット) ----
struct ImportedSlot
{
	std::array<float, 4> baseColor { 1, 1, 1, 1 };
	float                metallic    = 0.0f;
	float                roughness   = 0.6f;
	float                alphaCutoff = 0.0f;   // 切り抜き / 半透明でなければ 0
	std::string          baseColorTexture;     // アセットの ID (取り込まれていなければ空)
	std::string          normalTexture;
	std::string          metallicRoughnessTexture;
};
// 取り込んだ値を、よく使う変数名で並べる (baseColor / albedo / diffuseColor、baseColorMap / albedoMap / mainTex、
// normalMap、metallic、roughness、alphaCutoff、metallicRoughnessMap)
Json importedValues(const ImportedSlot& slot);

// 決まった値: imported (取り込んだ値。null 可) → object → slot の順に、変数ごとに上書きする
Json mergeNodeParams(const Json& imported, const Json& object, const Json& slot);
} // namespace studio
