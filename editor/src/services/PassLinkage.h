#pragma once
// ============================================================
//  editor/src/services/PassLinkage.h
//
//  パスの入力 / 出力と、パイプラインのレイアウト、シェーダーの宣言をつなぐ (2026-10-22)。
//    ・入力 (SRV) の bind (param, offset) → パイプラインの記述子テーブル → t レジスタ → シェーダーの変数
//    ・出力の RTV<n> → ピクセルシェーダーの SV_Target<n>、UAV の bind → u レジスタ → RWTexture2D など
//    ・ずれ: 届く先の変数がない入力、SV_Target がない RTV、どの入力もつないでいないシェーダーの変数
//    ・宣言を書き足す: 入力に合わせて `Texture2D <名前> : register(tN);`、RTV に合わせて SV_Target のメンバー
//  シェーダーの宣言は、ファイルの文字 (register を明示したもの) と、リフレクション (使っている変数) の両方から集める
//  (DXC のリフレクションは、宣言しても使っていない変数を返さないため)。
//  GPU も webview も使わない (tests/test_pass_linkage.cpp)。
// ============================================================

#include "gfx/abi/PipelineState.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace studio
{
// シェーダーのリソースの宣言 (t / u / b / s レジスタ)
struct ShaderDecl
{
	std::string name;
	std::string type;          // "Texture2D" / "Texture2D<float>" / "RWTexture2D<float4>" / "cbuffer" …
	char        cls   = 't';   // レジスタの種類 't' / 'u' / 'b' / 's'
	uint32_t    reg   = 0;
	uint32_t    space = 0;
	std::string file;          // アセットのパス (空ならリフレクションだけで分かったもの)
	int         line  = 0;     // 1 から (0 = 分からない)
	bool        used  = false; // シェーダーが使っている (リフレクションにある)
};

// ファイルの文字から、register を明示したリソースの宣言を集める (コメントは除く)
std::vector<ShaderDecl> parseShaderDecls(const std::string& text, const std::string& file);

// SV_Target<n> の宣言 (構造体のメンバー、関数の戻り値)。n を書いていなければ 0
struct TargetDecl
{
	uint32_t    index = 0;
	std::string name;   // メンバーの名前 (関数の戻り値なら空)
	int         line  = 0;
};
std::vector<TargetDecl> parseTargets(const std::string& text);

// 記述子テーブル (param) の offset 番目が指すレジスタ。テーブルでない / 範囲外なら nullopt
struct TableSlot
{
	char     cls   = 't';   // SRV = 't'、UAV = 'u'、CBV = 'b'
	uint32_t reg   = 0;
	uint32_t space = 0;
};
std::optional<TableSlot> tableSlotRegister(const GfxBindingLayoutDesc& layout, uint32_t param, uint32_t offset);
// レジスタ (cls, reg, space) を指すテーブルの位置 (param, offset)。params の中から探す。なければ nullopt
struct TablePos
{
	uint32_t param  = 0;
	uint32_t offset = 0;
};
std::optional<TablePos> tablePosOfRegister(const GfxBindingLayoutDesc& layout, const std::vector<uint32_t>& params, char cls, uint32_t reg,
                                           uint32_t space = 0);

// リソースの名前から、シェーダーの変数名を作る (英数字と _ にし、text にすでにある名前なら後ろに番号)
std::string shaderIdentifierFor(const std::string& resource, const std::string& text);

// 宣言を書き足す: 最後の register(t|u) の宣言の次の行、なければ最後の #include の次、なければ先頭のコメントの次
std::string insertResourceDecl(const std::string& text, const std::string& declLine, int* insertedLine = nullptr);
// SV_Target のメンバーを、SV_Target のメンバーを持つ構造体に足す。構造体がなければ nullopt (手で直す)
std::optional<std::string> insertTargetMember(const std::string& text, const std::string& type, const std::string& name, uint32_t index,
                                              int* insertedLine = nullptr);
} // namespace studio
