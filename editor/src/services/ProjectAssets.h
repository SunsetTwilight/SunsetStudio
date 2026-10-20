#pragma once
// ============================================================
//  editor/src/services/ProjectAssets.h
//
//  プロジェクトのアセットのライブラリ (M5-3): SunsetAsset のデータベースを、プロジェクトの assets/ にかぶせる。
//    assets/            取り込んだファイル (モデル、テクスチャ) と .meta (ID と取り込みの設定。git で管理する)
//    Cache/assets/      取り込んだ結果 (<id>.bin。消してもよい。古ければ作り直す)
//
//    importCopy  ファイルを assets/ にコピーして取り込む。モデルは assets/<名前>/ のフォルダに入れ、
//                参照しているテクスチャなども同じ相対パスでコピーする (元のフォルダの外のものはコピーせず、警告)
//    importLink  コピーせず、外部のファイルを参照する .meta だけを assets/ に作る
//    loadModel   取り込み (古ければ作り直す) とモデルの読み込み。データベースを触らないので、別のスレッドから呼んでよい
//
//  シーンはモデルを "asset:<ID>" で参照する (Scene.h)。GPU も webview も使わないので単体テストできる。
//  scan / importCopy / importLink は 1 つのスレッド (UI スレッド) から呼ぶこと。
// ============================================================

#include "sunset/asset/AssetDatabase.h"
#include "sunset/asset/model/Model.h"
#include "sunset/asset/texture/Texture.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace studio
{
class ProjectAssets
{
public:
	ProjectAssets();

	// projectDir/assets と projectDir/Cache/assets を使う (assets/ がなければ作る)。scan もする
	void open(const std::filesystem::path& projectDir, sunset::asset::Diagnostics& diag);
	void close();
	bool isOpen() const { return !m_root.empty(); }

	const std::filesystem::path& root() const     { return m_root; }
	const std::filesystem::path& cacheDir() const { return m_cache; }

	// assets/ を走査し直す (.meta のないファイルには作る)
	sunset::asset::AssetDatabase::ScanResult rescan(sunset::asset::Diagnostics& diag);

	const sunset::asset::AssetDatabase& database() const { return *m_db; }
	const sunset::asset::AssetRecord*   find(const std::string& id) const;

	// 一覧 (UI 用): [{ id, path, importer, external, source?, missing, cached }]。パスの順
	nlohmann::json listJson() const;

	struct ImportResult
	{
		std::string              id;        // 取り込んだアセットの ID
		std::string              path;      // assets/ からの相対パス
		std::vector<std::string> copied;    // 一緒にコピーしたファイル (assets/ からの相対パス)
		std::vector<std::string> warnings;
	};
	bool importCopy(const std::filesystem::path& file, ImportResult& result, std::string& error);
	bool importLink(const std::filesystem::path& file, ImportResult& result, std::string& error);

	// 取り込める拡張子 (".fbx" など。ファイルの選択の絞り込み用)
	std::vector<std::string> extensions(const std::string& importer = {}) const;

	// モデルを読む (キャッシュが古ければ取り込み直す)。record と cacheDir はコピーしたものを渡す。
	// 失敗したら nullptr (diag に理由)。どのスレッドから呼んでもよい (同じ ID を同時に読まないこと。取り込みの登録だけを使う)
	std::shared_ptr<const sunset::asset::Model> loadModel(const sunset::asset::AssetRecord& record,
	                                                       const std::filesystem::path& cacheDir,
	                                                       sunset::asset::Diagnostics& diag) const;

	// テクスチャを読む (loadModel と同じく、どのスレッドから呼んでもよい)
	std::shared_ptr<const sunset::asset::Texture> loadTexture(const sunset::asset::AssetRecord& record,
	                                                           const std::filesystem::path& cacheDir,
	                                                           sunset::asset::Diagnostics& diag) const;

	// モデルが参照しているファイル (元のファイルからの相対パス) のアセット。なければ nullptr
	const sunset::asset::AssetRecord* findReferenced(const sunset::asset::AssetRecord& model, const std::string& relative) const;

private:
	sunset::asset::ImporterRegistry               m_registry;
	std::unique_ptr<sunset::asset::AssetDatabase> m_db;
	std::filesystem::path                         m_root;
	std::filesystem::path                         m_cache;
};

// "asset:<ID>" の ID (小文字の 16 進 32 文字)。形が違えば nullopt
std::optional<std::string> assetIdOfModel(const std::string& model);
} // namespace studio
