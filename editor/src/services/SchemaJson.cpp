// ============================================================
//  editor/src/services/SchemaJson.cpp
// ============================================================
#include "SchemaJson.h"

#include <cstring>
#include <set>
#include <vector>

namespace studio
{
namespace
{
Json toNlohmann(const GfxJson& v)
{
	return Json::parse(gfxJsonStringify(v, 0));
}

bool toGfx(const Json& v, GfxJson& out, std::string& error)
{
	return gfxJsonParse(v.dump(), out, &error);
}
} // namespace

Json describeType(const std::string& typeName)
{
	const GfxStructInfo* root = gfxFindStructInfo(typeName.c_str());
	if (!root)
		return nullptr;

	// 入れ子の構造体と列挙型をたどって集める
	Json structs = Json::object();
	Json enums   = Json::object();
	std::vector<const GfxStructInfo*> pending { root };
	std::set<std::string> seen;
	while (!pending.empty())
	{
		const GfxStructInfo* si = pending.back();
		pending.pop_back();
		if (!seen.insert(si->name).second)
			continue;
		structs[si->name] = toNlohmann(gfxDescribeStruct(si));
		for (uint32_t i = 0; i < si->fieldCount; ++i)
		{
			const GfxFieldInfo& f = si->fields[i];
			if (f.structInfo)
				pending.push_back(f.structInfo);
			if (f.enumInfo && !enums.contains(f.enumInfo->name))
				enums[f.enumInfo->name] = toNlohmann(gfxDescribeEnum(f.enumInfo));
		}
	}
	return { { "type", root->name }, { "structs", structs }, { "enums", enums } };
}

Json listTypes()
{
	Json structs = Json::array();
	Json enums   = Json::array();
	uint32_t n = 0;
	const GfxStructInfo* const* sl = gfxAllStructInfos(n);
	for (uint32_t i = 0; i < n; ++i)
		structs.push_back(sl[i]->name);
	const GfxEnumInfo* const* el = gfxAllEnumInfos(n);
	for (uint32_t i = 0; i < n; ++i)
		enums.push_back(el[i]->name);
	return { { "structs", structs }, { "enums", enums } };
}

Json structToJson(const GfxStructInfo* si, const void* obj)
{
	// フォームに全部の値を出すので、既定値と同じフィールドも書く
	const GfxJsonWriteOptions opts{ true, {} };
	return toNlohmann(gfxStructToJson(si, obj, opts));
}

ApplyResult applyJson(const GfxStructInfo* si, const Json& json, void* obj)
{
	ApplyResult result;
	if (!si || !obj)
	{
		result.errors.push_back("型の情報がありません");
		return result;
	}
	GfxJson gj;
	std::string parseError;
	if (!toGfx(json, gj, parseError))
	{
		result.errors.push_back("JSON を読めません: " + parseError);
		return result;
	}

	// 失敗したときに obj を変えないよう、写しに書き込んでから戻す
	std::vector<uint8_t> copy(si->size);
	std::memcpy(copy.data(), obj, si->size);
	GfxJsonDiagnostics diag;
	result.ok       = gfxStructFromJson(si, gj, copy.data(), &diag);
	result.errors   = std::move(diag.errors);
	result.warnings = std::move(diag.warnings);
	if (result.ok)
		std::memcpy(obj, copy.data(), si->size);
	return result;
}
} // namespace studio
