// ============================================================
//  editor/src/main.cpp  (Phase 4 までのスタブ)
//
//  依存ライブラリが正しく取得・リンク・配置されていることを確認するだけの実行ファイル。
//    ・SunsetGraphicsEngine: 本体 DLL を初期化し、バックエンド DLL をロードしてアダプター名を表示
//    ・SunsetMathLib       : ベクトル演算
//    ・ImGui / imnodes / nlohmann_json: コンテキストの作成と JSON の出力
//  エディター本体は SunsetGraphicsEngine/docs/design/editor-architecture.md の Phase 4 で実装する。
// ============================================================
#include "gfx/core/GraphicsEngine.h"
#include "gfx/schema/GfxSchema.h"
#include "MathLib/Math.h"

#include <imgui.h>
#include <imnodes.h>
#include <nlohmann/json.hpp>

#include <iostream>

int main()
{
	// ---- SunsetGraphicsEngine ----
	GraphicsEngine engine;
	DeviceConfig   cfg;
	cfg.appName = "SunsetEditor";
#ifdef STUDIO_GFX_VALIDATION
	cfg.validation = true;
#endif
	if (!engine.init(cfg))
	{
		std::cerr << "[SunsetEditor] engine.init failed: " << engine.lastError() << "\n";
		return 1;
	}

	// ---- SunsetMathLib ----
	const Vector3 camera{ 1.5f, 2.0f, -2.5f };
	const float   distance = length(camera);

	// ---- ImGui / imnodes ----
	ImGui::CreateContext();
	ImNodes::CreateContext();

	nlohmann::json info = {
		{ "adapter",        engine.adapterName() },
		{ "gfxAbi",         GFX_ABI_VERSION },
		{ "cameraDistance", distance },
		{ "imgui",          IMGUI_VERSION },
		{ "json",           { NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH } },
	};
	std::cout << "[SunsetEditor] stub: " << info.dump() << "\n";

	ImNodes::DestroyContext();
	ImGui::DestroyContext();
	engine.shutdown();
	return 0;
}
