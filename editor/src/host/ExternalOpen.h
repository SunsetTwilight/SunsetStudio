#pragma once
// ============================================================
//  editor/src/host/ExternalOpen.h
//
//  ファイルをエディターの外のアプリで開く (シェーダー編集の「外部で開く」)。
//  どちらも、起動の結果を待つあいだ呼び出し元を止める (数秒まで)。UI スレッド以外から呼ぶこと。
// ============================================================

#include <filesystem>
#include <string>

namespace studio
{
// OS の既定のアプリで開く。関連付けがなければ false (error に理由)
bool openWithDefaultApp(const std::filesystem::path& file, std::string& error);

// VS Code (PATH の code コマンド) で開く。line > 0 ならその行へ移る。code がなければ false
bool openInVSCode(const std::filesystem::path& file, int line, std::string& error);
} // namespace studio
