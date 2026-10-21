// ============================================================
//  editor/src/services/PassLinkage.cpp
// ============================================================
#include "PassLinkage.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace studio
{
namespace
{
std::vector<std::string> splitLines(const std::string& text)
{
	std::vector<std::string> lines;
	std::string cur;
	for (char c : text)
	{
		if (c == '\n')
		{
			if (!cur.empty() && cur.back() == '\r')
				cur.pop_back();
			lines.push_back(cur);
			cur.clear();
		}
		else
			cur += c;
	}
	lines.push_back(cur);
	return lines;
}

// 行のコメント (// と、行をまたがない /* */) を除く。行をまたぐ /* */ は inBlock で引き継ぐ
std::string stripComments(const std::string& line, bool& inBlock)
{
	std::string out;
	for (size_t i = 0; i < line.size(); ++i)
	{
		if (inBlock)
		{
			if (line.compare(i, 2, "*/") == 0) { inBlock = false; ++i; }
			continue;
		}
		if (line.compare(i, 2, "//") == 0) break;
		if (line.compare(i, 2, "/*") == 0) { inBlock = true; ++i; continue; }
		out += line[i];
	}
	return out;
}

std::string newlineOf(const std::string& text)
{
	return text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
}

std::string joinLines(const std::vector<std::string>& lines, const std::string& nl)
{
	std::string out;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		out += lines[i];
		if (i + 1 < lines.size())
			out += nl;
	}
	return out;
}
} // namespace

std::vector<ShaderDecl> parseShaderDecls(const std::string& text, const std::string& file)
{
	// <型> <名前> [配列] : register(<種類><番号>[, space<番号>])
	static const std::regex re(R"(^\s*((?:RW)?(?:Texture\w*|StructuredBuffer|ByteAddressBuffer|Buffer|SamplerState|SamplerComparisonState)(?:\s*<[^>]*>)?)\s+(\w+)\s*(?:\[[^\]]*\])?\s*:\s*register\s*\(\s*([tusb])(\d+)\s*(?:,\s*space(\d+)\s*)?\))");
	static const std::regex cb(R"(^\s*cbuffer\s+(\w+)\s*:\s*register\s*\(\s*b(\d+)\s*(?:,\s*space(\d+)\s*)?\))");
	std::vector<ShaderDecl> out;
	bool inBlock = false;
	const auto lines = splitLines(text);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const std::string l = stripComments(lines[i], inBlock);
		std::smatch m;
		if (std::regex_search(l, m, re))
		{
			ShaderDecl d;
			d.type  = m[1].str();
			d.name  = m[2].str();
			d.cls   = m[3].str()[0];
			d.reg   = static_cast<uint32_t>(std::stoul(m[4].str()));
			d.space = m[5].matched ? static_cast<uint32_t>(std::stoul(m[5].str())) : 0;
			d.file  = file;
			d.line  = static_cast<int>(i) + 1;
			out.push_back(d);
		}
		else if (std::regex_search(l, m, cb))
		{
			ShaderDecl d;
			d.type  = "cbuffer";
			d.name  = m[1].str();
			d.cls   = 'b';
			d.reg   = static_cast<uint32_t>(std::stoul(m[2].str()));
			d.space = m[3].matched ? static_cast<uint32_t>(std::stoul(m[3].str())) : 0;
			d.file  = file;
			d.line  = static_cast<int>(i) + 1;
			out.push_back(d);
		}
	}
	return out;
}

std::vector<TargetDecl> parseTargets(const std::string& text)
{
	// メンバー: <型> <名前> : SV_Target<n>;   関数: ... ) : SV_Target<n>
	static const std::regex member(R"((\w+)\s*:\s*SV_Target(\d*)\s*;)", std::regex::icase);
	static const std::regex func(R"(\)\s*:\s*SV_Target(\d*))", std::regex::icase);
	std::vector<TargetDecl> out;
	bool inBlock = false;
	const auto lines = splitLines(text);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const std::string l = stripComments(lines[i], inBlock);
		std::smatch m;
		if (std::regex_search(l, m, member))
			out.push_back({ m[2].str().empty() ? 0u : static_cast<uint32_t>(std::stoul(m[2].str())), m[1].str(), static_cast<int>(i) + 1 });
		else if (std::regex_search(l, m, func))
			out.push_back({ m[1].str().empty() ? 0u : static_cast<uint32_t>(std::stoul(m[1].str())), std::string(), static_cast<int>(i) + 1 });
	}
	return out;
}

std::optional<TableSlot> tableSlotRegister(const GfxBindingLayoutDesc& l, uint32_t param, uint32_t offset)
{
	if (param >= l.numParameters)
		return std::nullopt;
	const GfxRootParameter& rp = l.parameters[param];
	if (rp.parameterType != GfxRootParameterType::DescriptorTable)
		return std::nullopt;
	uint32_t at = 0;
	for (uint32_t k = rp.firstRange; k < rp.firstRange + rp.numRanges && k < l.numRanges; ++k)
	{
		const GfxDescriptorRange& dr = l.ranges[k];
		const uint32_t start = dr.offsetInDescriptorsFromTableStart == GFX_DESCRIPTOR_RANGE_OFFSET_APPEND ? at : dr.offsetInDescriptorsFromTableStart;
		const bool unbounded = dr.numDescriptors == 0xFFFFFFFFu;
		if (offset >= start && (unbounded || offset < start + dr.numDescriptors))
		{
			const char cls = dr.rangeType == GfxDescriptorRangeType::SRV ? 't' : dr.rangeType == GfxDescriptorRangeType::UAV ? 'u'
			               : dr.rangeType == GfxDescriptorRangeType::CBV ? 'b' : 's';
			return TableSlot{ cls, dr.baseShaderRegister + (offset - start), dr.registerSpace };
		}
		if (unbounded)
			break;
		at = start + dr.numDescriptors;
	}
	return std::nullopt;
}

std::optional<TablePos> tablePosOfRegister(const GfxBindingLayoutDesc& l, const std::vector<uint32_t>& params, char cls, uint32_t reg, uint32_t space)
{
	for (uint32_t param : params)
	{
		if (param >= l.numParameters || l.parameters[param].parameterType != GfxRootParameterType::DescriptorTable)
			continue;
		const GfxRootParameter& rp = l.parameters[param];
		uint32_t at = 0;
		for (uint32_t k = rp.firstRange; k < rp.firstRange + rp.numRanges && k < l.numRanges; ++k)
		{
			const GfxDescriptorRange& dr = l.ranges[k];
			const uint32_t start = dr.offsetInDescriptorsFromTableStart == GFX_DESCRIPTOR_RANGE_OFFSET_APPEND ? at : dr.offsetInDescriptorsFromTableStart;
			const char rc = dr.rangeType == GfxDescriptorRangeType::SRV ? 't' : dr.rangeType == GfxDescriptorRangeType::UAV ? 'u'
			              : dr.rangeType == GfxDescriptorRangeType::CBV ? 'b' : 's';
			const bool unbounded = dr.numDescriptors == 0xFFFFFFFFu;
			if (rc == cls && dr.registerSpace == space && reg >= dr.baseShaderRegister && (unbounded || reg < dr.baseShaderRegister + dr.numDescriptors))
				return TablePos{ param, start + (reg - dr.baseShaderRegister) };
			if (unbounded)
				break;
			at = start + dr.numDescriptors;
		}
	}
	return std::nullopt;
}

std::string shaderIdentifierFor(const std::string& resource, const std::string& text)
{
	std::string id;
	for (char c : resource)
		id += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
	if (id.empty() || std::isdigit(static_cast<unsigned char>(id[0])))
		id = "t_" + id;
	auto taken = [&](const std::string& n) {
		const std::regex w("\\b" + n + "\\b");
		return std::regex_search(text, w);
	};
	if (!taken(id))
		return id;
	for (int k = 2;; ++k)
		if (!taken(id + std::to_string(k)))
			return id + std::to_string(k);
}

std::string insertResourceDecl(const std::string& text, const std::string& declLine, int* insertedLine)
{
	const std::string nl = newlineOf(text);
	std::vector<std::string> lines = splitLines(text);
	static const std::regex reg(R"(register\s*\(\s*[tu]\d+)");
	static const std::regex inc(R"(^\s*#\s*include\b)");
	int after = -1;
	bool inBlock = false;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const std::string l = stripComments(lines[i], inBlock);
		if (std::regex_search(l, reg))
			after = static_cast<int>(i);
	}
	if (after < 0)
	{
		inBlock = false;
		for (size_t i = 0; i < lines.size(); ++i)
			if (std::regex_search(stripComments(lines[i], inBlock), inc))
				after = static_cast<int>(i);
	}
	if (after < 0)
	{
		// 先頭のコメント (// の行) の次
		after = -1;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			const auto p = lines[i].find_first_not_of(" \t");
			if (p != std::string::npos && lines[i].compare(p, 2, "//") == 0)
				after = static_cast<int>(i);
			else
				break;
		}
	}
	lines.insert(lines.begin() + (after + 1), declLine);
	if (insertedLine)
		*insertedLine = after + 2;
	return joinLines(lines, nl);
}

std::optional<std::string> insertTargetMember(const std::string& text, const std::string& type, const std::string& name, uint32_t index,
                                              int* insertedLine)
{
	const std::string nl = newlineOf(text);
	std::vector<std::string> lines = splitLines(text);
	static const std::regex member(R"(\w+\s*:\s*SV_Target\d*\s*;)", std::regex::icase);
	int last = -1;
	std::string indent = "\t";
	bool inBlock = false;
	for (size_t i = 0; i < lines.size(); ++i)
		if (std::regex_search(stripComments(lines[i], inBlock), member))
		{
			last = static_cast<int>(i);
			indent = lines[i].substr(0, lines[i].find_first_not_of(" \t"));
		}
	if (last < 0)
		return std::nullopt;
	lines.insert(lines.begin() + (last + 1), indent + type + " " + name + " : SV_Target" + std::to_string(index) + ";");
	if (insertedLine)
		*insertedLine = last + 2;
	return joinLines(lines, nl);
}
} // namespace studio
