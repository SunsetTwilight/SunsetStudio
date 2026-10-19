// ============================================================
//  editor/src/host/FileDialog_win32.cpp  (IFileOpenDialog)
// ============================================================
#include "FileDialog.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace studio
{
namespace
{
template<typename T>
struct ComPtr
{
	T* p = nullptr;
	~ComPtr() { if (p) p->Release(); }
	T** operator&() { return &p; }
	T*  operator->() const { return p; }
	explicit operator bool() const { return p != nullptr; }
};

// COM は WebView2 が UI スレッドで初期化している。念のため、ここでも初期化する (同じモードなら参照が増えるだけ)
struct ComScope
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

std::optional<std::filesystem::path> runDialog(void* owner, bool folders, const std::filesystem::path& initial,
                                               const std::vector<std::string>* assetExtensions = nullptr)
{
	ComScope com;
	ComPtr<IFileOpenDialog> dlg;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
		return std::nullopt;
	DWORD opts = 0;
	dlg->GetOptions(&opts);
	opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
	if (folders)
		opts |= FOS_PICKFOLDERS;
	else
		opts |= FOS_FILEMUSTEXIST;
	dlg->SetOptions(opts);
	std::wstring spec, label;   // 絞り込みの文字列は Show が終わるまで残す
	if (folders)
		dlg->SetTitle(L"プロジェクトを作る場所");
	else if (assetExtensions)
	{
		dlg->SetTitle(L"アセットを取り込む");
		for (const std::string& e : *assetExtensions)
			spec += (spec.empty() ? L"*" : L";*") + std::filesystem::path(std::u8string(e.begin(), e.end())).wstring();
		label = L"取り込めるファイル (" + spec + L")";
		const COMDLG_FILTERSPEC filters[] = { { label.c_str(), spec.c_str() }, { L"すべてのファイル (*.*)", L"*.*" } };
		dlg->SetFileTypes(2, filters);
	}
	else
	{
		dlg->SetTitle(L"プロジェクトを開く");
		const COMDLG_FILTERSPEC filters[] = { { L"SunsetStudio のプロジェクト (*.sunsetproj)", L"*.sunsetproj" } };
		dlg->SetFileTypes(1, filters);
	}
	if (!initial.empty())
	{
		ComPtr<IShellItem> folder;
		if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder))))
			dlg->SetFolder(folder.p);
	}
	if (FAILED(dlg->Show(static_cast<HWND>(owner))))
		return std::nullopt;   // キャンセルも含む
	ComPtr<IShellItem> item;
	if (FAILED(dlg->GetResult(&item)))
		return std::nullopt;
	PWSTR path = nullptr;
	if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path)
		return std::nullopt;
	std::filesystem::path result(path);
	CoTaskMemFree(path);
	return result;
}
} // namespace

std::optional<std::filesystem::path> openProjectDialog(void* ownerWindow)
{
	return runDialog(ownerWindow, false, {});
}

std::optional<std::filesystem::path> pickFolderDialog(void* ownerWindow, const std::filesystem::path& initial)
{
	return runDialog(ownerWindow, true, initial);
}

std::optional<std::filesystem::path> openAssetDialog(void* ownerWindow, const std::vector<std::string>& extensions)
{
	return runDialog(ownerWindow, false, {}, &extensions);
}

std::filesystem::path documentsFolder()
{
	PWSTR path = nullptr;
	std::filesystem::path result;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path)) && path)
		result = path;
	CoTaskMemFree(path);
	return result;
}
} // namespace studio
