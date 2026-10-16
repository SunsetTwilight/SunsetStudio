cmake_minimum_required(VERSION 3.24)
project(SunsetStudio VERSION 0.1.0 LANGUAGES CXX)

# ============================================================
#  SunsetStudio
#
#  SunsetGraphicsEngine (描画) と SunsetMathLib (数学) を組み合わせるアプリケーション。
#  最初のアプリはエディター (editor/)。
#
#  依存ライブラリは cmake/StudioDependencies.cmake で取得する。
#    ・既定: GitHub のタグ / ブランチを FetchContent で取得 (install 済みのパッケージがあれば優先)
#    ・開発用プリセット (*-dev): 同じワークスペースの隣のフォルダ
#      (../SunsetGraphicsEngine、../SunsetMathLib) を直接使い、ライブラリとアプリを同時に編集できる
# ============================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 実行ファイルを build/<preset>/bin/ にまとめる (DLL は sunset_gfx_deploy_runtime で横に配置する)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${_cfg} _CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/bin")
endforeach()

include(StudioOptions)        # studio_options (警告・定義の共通設定)
include(StudioDependencies)   # SunsetGraphicsEngine / MathLib / ImGui / imnodes / nlohmann_json

add_subdirectory(editor)

option(STUDIO_BUILD_TESTS "自動テスト (doctest) をビルドする。configure 時に doctest を取得する" ON)
if(STUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
