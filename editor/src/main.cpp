// ============================================================
//  editor/src/main.cpp
//
//  SunsetEditor の起動。設計は docs/design/editor-shell.md。
//    UI スレッド (このスレッド): webview のウィンドウ、C++ と JS の橋渡し
//    描画スレッド             : エンジン
//
//  コマンドライン / 環境変数:
//    --software / STUDIO_ADAPTER=software   WARP (ソフトウェアレンダラー) を使う
//    --devtools / STUDIO_DEVTOOLS=1          開発者ツールを使えるようにする (Debug ビルドでは常に)
//    STUDIO_UI_DEV_URL=http://localhost:5173 Vite の開発サーバーの UI を開く (ホットリロード)
//    --smoke-test                            UI とエンジンが動いたことを確認して終了する (自動テスト用)
//    <ファイル>.sunsetproj                    そのプロジェクトを開いて起動する (なければ起動画面を出す)
// ============================================================
#include "app/EditorApp.h"

#include <filesystem>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW

namespace
{
std::filesystem::path executableDir()
{
	wchar_t buf[MAX_PATH * 2] = {};
	const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
	if (n == 0 || n >= std::size(buf))
		return std::filesystem::current_path();
	return std::filesystem::path(buf).parent_path();
}
} // namespace

int main(int argc, char** argv)
{
	studio::EditorOptions options = studio::EditorOptions::fromEnvironment(argc, argv);
	const std::filesystem::path exeDir = executableDir();
	options.uiIndex = exeDir / "ui" / "index.html";
	options.templateRoot = exeDir / "templates";
#ifdef STUDIO_TEMPLATE_SOURCE_DIR
	// ビルドツリーでは、ソースの editor/templates を使う
	{
		const std::string utf8 = STUDIO_TEMPLATE_SOURCE_DIR;
		std::error_code   ec;
		const std::filesystem::path source(std::u8string(utf8.begin(), utf8.end()));
		if (std::filesystem::is_directory(source, ec))
			options.templateRoot = source;
	}
#endif
	// コマンドラインのプロジェクトファイル (UTF-16 で受け取る)
	{
		int     wargc = 0;
		LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
		for (int i = 1; wargv && i < wargc; ++i)
		{
			const std::filesystem::path a(wargv[i]);
			if (a.extension() == L".sunsetproj")
				options.projectFile = a;
		}
		LocalFree(wargv);
	}

	studio::EditorApp app(std::move(options));
	return app.run();
}
