// ============================================================
//  パスの入力 / 出力とシェーダーの宣言のつながり (PassLinkage) の単体テスト (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/PassLinkage.h"

using namespace studio;

namespace
{
// b0 / b1 と、t0〜t2 (SRV 3 つ) のテーブル、u1 (UAV 1 つ) のテーブル
GfxBindingLayoutDesc layout()
{
	GfxBindingLayoutDesc l;
	l.numParameters = 4;
	l.parameters[0].parameterType = GfxRootParameterType::CBV;
	l.parameters[0].shaderRegister = 0;
	l.parameters[1].parameterType = GfxRootParameterType::CBV;
	l.parameters[1].shaderRegister = 1;
	l.parameters[2].parameterType = GfxRootParameterType::DescriptorTable;
	l.parameters[2].firstRange = 0;
	l.parameters[2].numRanges  = 1;
	l.parameters[3].parameterType = GfxRootParameterType::DescriptorTable;
	l.parameters[3].firstRange = 1;
	l.parameters[3].numRanges  = 1;
	l.numRanges = 2;
	l.ranges[0].rangeType = GfxDescriptorRangeType::SRV;
	l.ranges[0].numDescriptors = 3;
	l.ranges[0].baseShaderRegister = 0;
	l.ranges[1].rangeType = GfxDescriptorRangeType::UAV;
	l.ranges[1].numDescriptors = 1;
	l.ranges[1].baseShaderRegister = 1;
	return l;
}

const char* k_PS = R"(// テスト
#include "common.hlsli"

Texture2D        gAlbedo : register(t0);
Texture2D<float> gDepth  : register(t2);   // 深度
// Texture2D gOld : register(t5);
/* Texture2D gBlock : register(t6); */
RWTexture2D<float4> gOut : register(u1);
SamplerState gSampler : register(s0);
cbuffer Params : register(b2) { float4 c; };

struct MRT
{
	float4 albedo : SV_Target0;
	float4 normal : SV_Target1;
};
)";
} // namespace

TEST_CASE("PassLinkage: 宣言を読む (コメントは除く)")
{
	const auto d = parseShaderDecls(k_PS, "shaders/a.ps.hlsl");
	REQUIRE(d.size() == 5);
	CHECK(d[0].name == "gAlbedo");
	CHECK(d[0].cls == 't');
	CHECK(d[0].reg == 0);
	CHECK(d[0].line == 4);
	CHECK(d[1].type == "Texture2D<float>");
	CHECK(d[1].reg == 2);
	CHECK(d[2].name == "gOut");
	CHECK(d[2].cls == 'u');
	CHECK(d[3].cls == 's');
	CHECK(d[4].type == "cbuffer");
	CHECK(d[4].reg == 2);

	const auto t = parseTargets(k_PS);
	REQUIRE(t.size() == 2);
	CHECK(t[1].index == 1);
	CHECK(t[1].name == "normal");
	const auto f = parseTargets("float4 main(PSIn i) : SV_Target\n{");
	REQUIRE(f.size() == 1);
	CHECK(f[0].index == 0);
	CHECK(f[0].type == "float4");
	CHECK(t[0].type == "float4");
	const auto u = parseTargets("struct O\n{\n\tuint   mask   : SV_Target2;   // マスク\n};");
	REQUIRE(u.size() == 1);
	CHECK(u[0].type == "uint");
	CHECK(u[0].name == "mask");
	CHECK(u[0].index == 2);
}

TEST_CASE("PassLinkage: 型とフォーマットのずれ")
{
	CHECK(shaderScalarKind("Texture2D") == ScalarKind::Float);
	CHECK(shaderScalarKind("Texture2D<float>") == ScalarKind::Float);
	CHECK(shaderScalarKind("Texture2D<uint>") == ScalarKind::Uint);
	CHECK(shaderScalarKind("RWTexture2D<int2>") == ScalarKind::Sint);
	CHECK(shaderScalarKind("min16float4") == ScalarKind::Float);
	CHECK(shaderScalarKind("") == ScalarKind::Unknown);
	CHECK(formatScalarKind("R32_Uint") == ScalarKind::Uint);
	CHECK(formatScalarKind("R16G16_Sint") == ScalarKind::Sint);
	CHECK(formatScalarKind("D32_Float") == ScalarKind::Float);
	CHECK(formatScalarKind("R8G8B8A8_Unorm") == ScalarKind::Float);

	CHECK(typeMismatch("float4", "R16G16B16A16_Float").empty());
	CHECK(typeMismatch("Texture2D<float>", "D32_Float").empty());
	CHECK(typeMismatch("uint", "R32_Uint").empty());
	CHECK(typeMismatch("", "R32_Uint").empty());   // 型が分からなければ何も言わない
	CHECK(typeMismatch("float4", "").empty());
	const std::string m = typeMismatch("uint", "R8G8B8A8_Unorm");
	CHECK(m.find("uint") != std::string::npos);
	CHECK(m.find("R8G8B8A8_Unorm") != std::string::npos);
	CHECK_FALSE(typeMismatch("Texture2D", "R32_Uint").empty());
}

TEST_CASE("PassLinkage: bind ⇔ レジスタ")
{
	const GfxBindingLayoutDesc l = layout();
	auto s = tableSlotRegister(l, 2, 2);
	REQUIRE(s);
	CHECK(s->cls == 't');
	CHECK(s->reg == 2);
	CHECK_FALSE(tableSlotRegister(l, 2, 3));   // 範囲外
	CHECK_FALSE(tableSlotRegister(l, 0, 0));   // CBV
	s = tableSlotRegister(l, 3, 0);
	REQUIRE(s);
	CHECK(s->cls == 'u');
	CHECK(s->reg == 1);

	auto p = tablePosOfRegister(l, { 2, 3 }, 't', 1);
	REQUIRE(p);
	CHECK(p->param == 2);
	CHECK(p->offset == 1);
	CHECK_FALSE(tablePosOfRegister(l, { 2 }, 't', 5));
	CHECK_FALSE(tablePosOfRegister(l, { 3 }, 't', 1));   // param 3 は UAV
}

TEST_CASE("PassLinkage: 宣言を書き足す")
{
	CHECK(shaderIdentifierFor("gbAlbedo", k_PS) == "gbAlbedo");
	CHECK(shaderIdentifierFor("gAlbedo", k_PS) == "gAlbedo2");
	CHECK(shaderIdentifierFor("my-tex.1", "") == "my_tex_1");

	int line = 0;
	const std::string a = insertResourceDecl(k_PS, "Texture2D gNew : register(t3);", &line);
	CHECK(line == 9);   // 最後の register(t|u) の宣言 (gOut、8 行目) の次
	CHECK(parseShaderDecls(a, "x").size() == 6);
	// 宣言がなければ #include の次
	const std::string b = insertResourceDecl("// c\n#include \"x.hlsli\"\n\nfloat4 main() : SV_Target { return 0; }", "Texture2D t : register(t0);", &line);
	CHECK(line == 3);
	CHECK(b.find("#include \"x.hlsli\"\nTexture2D t : register(t0);") != std::string::npos);

	const auto m = insertTargetMember(k_PS, "float4", "extra", 2, &line);
	REQUIRE(m);
	CHECK(parseTargets(*m).size() == 3);
	CHECK(parseTargets(*m)[2].index == 2);
	CHECK_FALSE(insertTargetMember("float4 main() : SV_Target { return 0; }", "float4", "x", 1));
}
