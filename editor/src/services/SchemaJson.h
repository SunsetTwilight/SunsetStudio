#pragma once
// ============================================================
//  editor/src/services/SchemaJson.h
//
//  エンジンのスキーマ (gfx/schema) と、Bridge の JSON (nlohmann::json) の橋渡し。
//  GPU も webview も使わないので単体テストできる (tests/test_schema_json.cpp)。
//
//    describeType   : 型の説明 (フィールド表) と、その中で使っている構造体 / 列挙型の説明をまとめて返す
//    listTypes      : スキーマに登録された構造体 / 列挙型の名前
//    structToJson   : 記述子 → JSON (既定値のフィールドも含めてすべて。ハンドルと const char* は含めない)
//    applyJson      : JSON を記述子に上書きする (書かれているフィールドだけ。ハンドルなどはそのまま)
//
//  エンジンの GfxJson と nlohmann::json は、文字列を経由して変換する。
// ============================================================

#include "gfx/schema/GfxJson.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

// 型名が見つからなければ null
Json describeType(const std::string& typeName);
Json listTypes();

Json structToJson(const GfxStructInfo* si, const void* obj);

struct ApplyResult
{
	bool                     ok = false;
	std::vector<std::string> errors;     // "rasterizerState.cullMode: ..." の形
	std::vector<std::string> warnings;
};
// 失敗しても obj は変えない (成功したときだけ書き込む)
ApplyResult applyJson(const GfxStructInfo* si, const Json& json, void* obj);

template<typename T>
ApplyResult applyJson(const Json& json, T& value)
{
	return applyJson(gfxStructInfo<T>(), json, &value);
}
} // namespace studio
