# ============================================================
#  SunsetStudio の自動テスト (doctest + CTest)
#
#    studio_tests  両ライブラリの結合テスト
#                  (エンジンの DLL の配置と初期化、MathLib、スキーマ)
#                  環境変数 GFX_TEST_ADAPTER=software で WARP を使う (GPU のない CI 用)
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
