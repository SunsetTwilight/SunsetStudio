#pragma once
// ============================================================
//  editor/src/services/AssetFiles.h
//
//  プロジェクトのアセット (shaders/*.hlsl、pipelines/*.json など) をファイルとして扱う (M4 のシェーダー編集)。
//    ・アセットのパス: アセットのフォルダからの相対パス、区切りは "/" ("shaders/lighting.ps.hlsl")。
//      フォルダの外 (絶対パス、".." で外へ出る) は受け付けない
//    ・新しく作るときのテンプレート (シェーダー、パイプライン)
//  GPU も webview も使わない (単体テストする)。
// ============================================================

#include <filesystem>
#include <string>
#include <vector>

namespace studio
{
// rel をアセットのフォルダ root の中の絶対パスにする。外を指していれば false (error に理由)
bool resolveAssetPath(const std::filesystem::path& root, const std::string& rel, std::filesystem::path& out, std::string& error);

// abs が root の中ならアセットのパス ("shaders/x.hlsl")、外なら空
std::string assetRelativePath(const std::filesystem::path& root, const std::filesystem::path& abs);

// root/dir の下 (サブフォルダも) で、拡張子 (".hlsl" など。空なら全部) が合うファイルのアセットのパスを、名前の順に返す
std::vector<std::string> listAssetFiles(const std::filesystem::path& root, const std::string& dir,
                                        const std::vector<std::string>& extensions);

// シェーダーのステージ ("vs" / "ps" / "cs")。ファイル名の ".ps.hlsl" などから推測する。分からなければ空
std::string guessShaderStage(const std::string& path);

// ---- 新しく作るときのテンプレート ----
struct AssetTemplate
{
	std::string id;          // "shader.fullscreen.ps" など
	std::string kind;        // "shader" / "pipeline"
	std::string label;       // 画面に出す名前
	std::string folder;      // 既定の置き場所 ("shaders" / "pipelines")
	std::string suffix;      // ファイル名の末尾 (".ps.hlsl" / ".json")
	std::string stage;       // シェーダーのとき: "vs" / "ps" / "cs"
	std::string description;
};
const std::vector<AssetTemplate>& assetTemplates();
const AssetTemplate*              findAssetTemplate(const std::string& id);

// テンプレートの中身。パイプラインのテンプレートでは shader に PS (Compute なら CS) のアセットのパスを入れる。
// 描画ノードのパイプライン ("pipeline.draw") は vs に VS のパスも入れる。テンプレートがなければ false
bool assetTemplateText(const std::string& id, const std::string& shader, std::string& out, const std::string& vs = {});
} // namespace studio
