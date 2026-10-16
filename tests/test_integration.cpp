// ============================================================
//  両ライブラリの結合テスト
//    ・エンジン: 配置された本体 DLL / バックエンド DLL をロードし、ABI が一致して初期化できる
//    ・MathLib : 基本的なベクトル演算
//    ・スキーマ: エディターのインスペクターが使うメタデータ表が引ける
//  環境変数 GFX_TEST_ADAPTER=software で WARP を使う。
// ============================================================
#include <doctest/doctest.h>
#include "gfx/core/GraphicsEngine.h"
#include "gfx/schema/GfxSchema.h"
#include "MathLib/Math.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace
{
bool useSoftwareAdapter()
{
	char   buf[32] = {};
	size_t len     = 0;
	return getenv_s(&len, buf, sizeof(buf), "GFX_TEST_ADAPTER") == 0 && len > 0
		&& std::string(buf) == "software";
}
} // namespace

TEST_CASE("エンジン: 配置された DLL を読み込んで初期化できる")
{
	GfxABIInfo dllInfo = {};
	gfxCoreGetABIInfo(&dllInfo);
	CHECK(dllInfo == gfxCurrentABIInfo());   // 取得したエンジンのヘッダーと DLL が一致している

	GraphicsEngine engine;
	DeviceConfig   cfg;
	cfg.appName = "studio_tests";
	cfg.adapter = useSoftwareAdapter() ? GfxAdapterType::Software : GfxAdapterType::Hardware;
	REQUIRE_MESSAGE(engine.init(cfg), engine.lastError());
	MESSAGE("adapter: ", engine.adapterName());
	CHECK(engine.device() != nullptr);
	engine.shutdown();
	CHECK_FALSE(engine.isInitialized());
}

TEST_CASE("MathLib: ベクトルの長さ")
{
	const Vector3 v{ 3.0f, 4.0f, 0.0f };
	CHECK(length(v) == doctest::Approx(5.0f));
}

TEST_CASE("スキーマ: パイプラインの記述子のメタデータ表が引ける")
{
	const GfxStructInfo* si = gfxStructInfo<GfxGraphicsPipelineDesc>();
	REQUIRE(si != nullptr);
	CHECK(si->fieldCount > 0);
}
