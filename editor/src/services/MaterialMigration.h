#pragma once
// ============================================================
//  editor/src/services/MaterialMigration.h
//
//  マテリアルの仕組み (M5-4) を外したときの、既存のプロジェクトの移行 (2026-10-22)。
//  影のパスを、マテリアルの影のパイプライン (pipelines/materials/standard.shadow.json) から、テンプレートの
//  pipelines/shadow.json (深さだけ) に替える。開くときに調べ、確認してから書き換える。
//  描画ノードのパイプライン / シェーダー (b2 の定数と t0〜 のテクスチャを使うもの) は、描画ノードのリソース
//  (物体ごとの値、services/NodeParams.h) でそのまま描けるので、書き換えない。
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
	std::vector<std::string> manual;   // 手で直すもの (説明。今はない)
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
