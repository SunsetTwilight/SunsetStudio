# ============================================================
#  SunsetEditor (Phase 4)
#
#  依存:
#    SunsetGraphicsEngine::core / ::schema  描画 (バックエンドは本体 DLL が実行時にロードする)
#    MathLib::MathLib                       数学 (カメラ、変換など)
#    ImGui / imnodes / nlohmann_json         UI、ノードエディター、アセットの JSON
#
#  現在は依存ライブラリの取得・リンク・DLL の配置を確認するための最小限の実行ファイル。
#  エディター本体は SunsetGraphicsEngine/docs/design/editor-architecture.md の Phase 4 で実装する。
# ============================================================
add_executable(SunsetEditor
    src/main.cpp
)

target_link_libraries(SunsetEditor PRIVATE
    SunsetGraphicsEngine::core
    SunsetGraphicsEngine::schema
    MathLib::MathLib
    studio_imgui
    studio_imnodes
    nlohmann_json::nlohmann_json
    studio_options
)

# 本体 DLL / バックエンド DLL / dxcompiler.dll を exe の横に配置する
sunset_gfx_deploy_runtime(SunsetEditor)

set_target_properties(SunsetEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(GNUInstallDirs)
install(TARGETS SunsetEditor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
