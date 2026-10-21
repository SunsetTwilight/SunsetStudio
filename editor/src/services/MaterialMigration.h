#pragma once
// ============================================================
//  editor/src/services/MaterialMigration.h
//
//  マテリアルの仕組み (M5-4) を外したときの、既存のプロジェクトの移行 (2026-10-22)。
//  マテリアルのころのプロジェクトは、描画のパイプライン / シェーダーがマテリアルの定数 (b2) とテクスチャ (t0) を
//  前提にしているので、そのままでは描けない。開くときに調べ、確認してから書き換える。
//    ・影のパス: pipelines/materials/standard.shadow.json → pipelines/shadow.json (テンプレートから足す)
//    ・描画ノードの既定のパイプラインと PS: マテリアルのころのテンプレートと同じ中身なら、今のテンプレートに置き換える
//    ・自分で書き換えたもの (b2 / t0 を使うレイアウト、cbuffer Material) は触らず、手で直すものとして挙げる
//  materials/ のファイルとシーンの割り当ては残す (読まない)。
//  GPU も webview も使わない (tests/test_material_migration.cpp)。
// ============================================================

#include <filesystem>
#include <string>
#include <vector>

namespace studio
{
struct MaterialMigrationPlan
{
	struct Change
	{
		std::string file;   // アセットのパス ("pipelines/shadow.json")
		std::string text;   // 書く中身
		std::string what;   // 画面に出す説明
	};
	std::vector<Change>      changes;
	std::vector<std::string> manual;   // 手で直すもの (説明)
	bool needed() const { return !changes.empty(); }
};

// root はプロジェクトのフォルダ、graphFile はビューポートのグラフ (root からの相対パス)、
// templateRoot はテンプレートのフォルダ (deferred/ の影のファイルを使う)
MaterialMigrationPlan planMaterialRemoval(const std::filesystem::path& root, const std::string& graphFile,
                                          const std::filesystem::path& templateRoot);

// 書く。書いたファイルを written に入れる。失敗したら false (error に理由。途中まで書いたものはそのまま)
bool applyMaterialRemoval(const std::filesystem::path& root, const MaterialMigrationPlan& plan,
                          std::vector<std::string>& written, std::string& error);
} // namespace studio
