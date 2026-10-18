# ============================================================
#  SunsetStudio の自動テスト (doctest + CTest)
#
#    studio_tests       両ライブラリの結合テスト
#                       (エンジンの DLL の配置と初期化、MathLib、スキーマ)
#                       環境変数 GFX_TEST_ADAPTER=software で WARP を使う (GPU のない CI 用)
#    studio_tests_unit  GPU も webview も使わない単体テスト (Bridge、スキーマと JSON の橋渡し、プロジェクトファイル、編集の履歴)
#    ui_unit            UI の単体テスト (Vitest。editor/frontend の npm run test)
#    editor_smoke       SunsetEditor --smoke-test --software: ウィンドウを開き、UI の ui.ready / app.info と
#                       エンジンの初期化 (WARP) を確認して終了する (WebView2 ランタイムとデスクトップが必要)
#
#  実行: ctest --test-dir build/<preset> --output-on-failure
# ============================================================
include(FetchContent)

# doctest はヘッダー 1 つだけを使う (同梱の CMake はテストやサンプルもビルドするため使わない)
set(STUDIO_DOCTEST_VERSION "2.5.2")
FetchContent_Declare(doctest
    URL      https://github.com/doctest/doctest/archive/refs/tags/v${STUDIO_DOCTEST_VERSION}.tar.gz
    URL_HASH SHA256=9189960c2bbbc4f3382ce0773b2bb5f13e3afd8fed47f55f193e11e85a4f9854
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR  _studio_no_cmake_
)
FetchContent_MakeAvailable(doctest)

add_library(studio_doctest INTERFACE)
target_include_directories(studio_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")

add_executable(studio_tests
    doctest_main.cpp
    test_integration.cpp
)
target_link_libraries(studio_tests PRIVATE
    SunsetGraphicsEngine::core
    SunsetGraphicsEngine::schema
    MathLib::MathLib
    studio_doctest
    studio_options
)
sunset_gfx_deploy_runtime(studio_tests)

add_test(NAME integration COMMAND studio_tests)
set_tests_properties(integration PROPERTIES LABELS "gpu")

# ---- 単体テスト (GPU / webview 不要) ----
add_executable(studio_tests_unit
    doctest_main.cpp
    test_bridge.cpp
    test_schema_json.cpp
    test_project.cpp
    test_undo.cpp
    test_asset_files.cpp
    test_scene.cpp
)
target_link_libraries(studio_tests_unit PRIVATE studio_bridge studio_services studio_doctest studio_options)
add_test(NAME unit COMMAND studio_tests_unit)

# ---- UI の単体テスト (Vitest。ノードエディターのモデルなど) ----
if(TARGET studio_ui)
    add_test(NAME ui_unit COMMAND "${STUDIO_NPM}" run test
             WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/editor/frontend")
    set_tests_properties(ui_unit PROPERTIES LABELS "ui" TIMEOUT 120)
endif()

# ---- エディターのスモークテスト ----
if(TARGET studio_ui)
    add_test(NAME editor_smoke COMMAND SunsetEditor --smoke-test --software)
    set_tests_properties(editor_smoke PROPERTIES LABELS "gui" TIMEOUT 120)
endif()
