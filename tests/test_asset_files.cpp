// ============================================================
//  アセットのファイル (パスの検証、一覧、テンプレート) の単体テスト (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/AssetFiles.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace studio;
namespace fs = std::filesystem;

namespace
{
fs::path freshDir(const char* name)
{
	const fs::path d = fs::temp_directory_path() / name;
	std::error_code ec;
	fs::remove_all(d, ec);
	fs::create_directories(d);
	return d;
}

void touch(const fs::path& p)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << "x";
}
} // namespace

TEST_CASE("アセットのパス: フォルダの中だけを受け付ける")
{
	const fs::path root = fs::path("C:/proj/Assets");
	fs::path out;
	std::string error;
	CHECK(resolveAssetPath(root, "shaders/a.hlsl", out, error));
	CHECK(out == fs::path("C:/proj/Assets/shaders/a.hlsl").lexically_normal());
	CHECK(resolveAssetPath(root, "shaders/../pipelines/b.json", out, error));
	CHECK(assetRelativePath(root, out) == "pipelines/b.json");
	CHECK_FALSE(resolveAssetPath(root, "../secret.txt", out, error));
	CHECK_FALSE(resolveAssetPath(root, "shaders/../../x", out, error));
	CHECK_FALSE(resolveAssetPath(root, "C:/Windows/win.ini", out, error));
	CHECK_FALSE(resolveAssetPath(root, "/etc/passwd", out, error));
	CHECK_FALSE(resolveAssetPath(root, "", out, error));
	CHECK(assetRelativePath(root, fs::path("C:/other/x.hlsl")).empty());
}

TEST_CASE("アセットの一覧とステージの推測")
{
	const fs::path root = freshDir("studio_asset_files_test");
	touch(root / "shaders" / "a.ps.hlsl");
	touch(root / "shaders" / "sub" / "b.CS.hlsl");
	touch(root / "shaders" / "common.hlsli");
	touch(root / "pipelines" / "p.json");
	CHECK(listAssetFiles(root, "shaders", { ".hlsl" }) == std::vector<std::string>{ "shaders/a.ps.hlsl", "shaders/sub/b.CS.hlsl" });
	CHECK(listAssetFiles(root, "shaders", {}).size() == 3);
	CHECK(listAssetFiles(root, "../", { ".json" }).empty());
	CHECK(guessShaderStage("shaders/a.ps.hlsl") == "ps");
	CHECK(guessShaderStage("shaders/sub/b.CS.hlsl") == "cs");
	CHECK(guessShaderStage("shaders/blur_cs.hlsl") == "cs");
	CHECK(guessShaderStage("shaders/common.hlsli").empty());
}

TEST_CASE("テンプレート")
{
	std::string text;
	for (const auto& t : assetTemplates())
	{
		CHECK(findAssetTemplate(t.id) == &t);
		CHECK(assetTemplateText(t.id, "shaders/x.ps.hlsl", text));
		CHECK_FALSE(text.empty());
	}
	CHECK(assetTemplateText("pipeline.fullscreen", "shaders/mine.ps.hlsl", text));
	CHECK(text.find("\"shaders/mine.ps.hlsl\"") != std::string::npos);
	// 描画ノードのパイプラインは VS と PS の両方を入れる (JSON として読める)
	CHECK(assetTemplateText("pipeline.draw", "shaders/toon.ps.hlsl", text, "shaders/toon.vs.hlsl"));
	CHECK(text.find("\"shaders/toon.vs.hlsl\"") != std::string::npos);
	CHECK(text.find("\"shaders/toon.ps.hlsl\"") != std::string::npos);
	CHECK_NOTHROW((void)nlohmann::json::parse(text));
	CHECK(text.find("depthEnable") == std::string::npos);
	REQUIRE(assetTemplateText("pipeline.draw.nodepth", "shaders/toon.ps.hlsl", text, "shaders/toon.vs.hlsl"));
	CHECK(nlohmann::json::parse(text)["state"]["depthStencilState"]["depthEnable"] == false);
	// 描画ノードのシェーダーは include しない (空のテンプレートには common.hlsli がない)
	for (const char* id : { "shader.draw.vs", "shader.draw.ps" })
	{
		REQUIRE(assetTemplateText(id, "", text));
		CHECK(text.find("#include") == std::string::npos);
		CHECK(text.find("cbuffer Frame") != std::string::npos);
	}
	CHECK_FALSE(assetTemplateText("nope", "", text));
	CHECK(findAssetTemplate("nope") == nullptr);
}
