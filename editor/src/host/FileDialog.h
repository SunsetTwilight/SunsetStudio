#pragma once
// ============================================================
//  editor/src/host/FileDialog.h
//
//  OS のファイル / フォルダの選択ダイアログ (M4a の「開く」「新規作成」)。UI スレッドから呼ぶ。
//  ダイアログが閉じるまで戻らない (そのあいだもウィンドウのメッセージは処理される)。
// ============================================================

#include <filesystem>
#include <optional>

namespace studio
{
// .sunsetproj を選ぶ。キャンセルなら nullopt
std::optional<std::filesystem::path> openProjectDialog(void* ownerWindow);
// フォルダを選ぶ。キャンセルなら nullopt
std::optional<std::filesystem::path> pickFolderDialog(void* ownerWindow, const std::filesystem::path& initial);
// ユーザーのドキュメントのフォルダ (新規作成の既定の場所の親)
std::filesystem::path documentsFolder();
} // namespace studio
