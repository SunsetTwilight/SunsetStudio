// ============================================================
//  editor/src/host/ExternalOpen_win32.cpp
// ============================================================
#include "ExternalOpen.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace studio
{
bool openWithDefaultApp(const std::filesystem::path& file, std::string& error)
{
	const HINSTANCE r = ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, file.parent_path().c_str(), SW_SHOWNORMAL);
	const auto code = reinterpret_cast<INT_PTR>(r);
	if (code > 32)
		return true;
	error = code == SE_ERR_NOASSOC || code == SE_ERR_ASSOCINCOMPLETE
		? "このファイルの種類を開くアプリが決まっていません (Windows の設定で .hlsl / .json の既定のアプリを選ぶか、VS Code で開いてください)"
		: "ファイルを開けませんでした (ShellExecute のエラー " + std::to_string(code) + ")";
	return false;
}

bool openInVSCode(const std::filesystem::path& file, int line, std::string& error)
{
	// code は code.cmd (バッチ) なので cmd.exe 経由で起動する。-g でファイルと行を指定する
	std::wstring target = file.wstring();
	if (line > 0)
		target += L":" + std::to_wstring(line);
	std::wstring cmd = L"cmd.exe /d /c code -g \"" + target + L"\"";

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		error = "cmd.exe を起動できませんでした";
		return false;
	}
	DWORD exitCode = 0;
	const bool finished = WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if (finished && exitCode != 0)
	{
		error = "VS Code の code コマンドが見つかりません (VS Code の「Shell Command: Install 'code' command in PATH」、または PATH を確認してください)";
		return false;
	}
	return true;
}
} // namespace studio
