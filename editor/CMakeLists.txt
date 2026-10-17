# ============================================================
#  SunsetEditor
#
#  設計: docs/design/editor-shell.md
#    UI      : webview/webview + Web UI (editor/frontend、React + TypeScript + Vite)
#    描画    : SunsetGraphicsEngine (描画スレッドが持つ)
#
#  ターゲット:
#    studio_bridge  C++ と JS の RPC (webview に依存しない。単体テストする)
#    SunsetEditor   エディターの実行ファイル
#    studio_ui      フロントエンドのビルド (STUDIO_BUILD_UI=ON のとき) → bin/ui/index.html
# ============================================================

# ---- C++ と JS の RPC (webview に依存しない) ----
add_library(studio_bridge STATIC
    src/host/Bridge.h
    src/host/Bridge.cpp
)
target_include_directories(studio_bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(studio_bridge
    PUBLIC  nlohmann_json::nlohmann_json
    PRIVATE studio_options
)

# ---- エディター ----
add_executable(SunsetEditor
    src/main.cpp
    src/app/EditorApp.h
    src/app/EditorApp.cpp
    src/host/WebHost.h
    src/host/WebHost.cpp
    src/render/RenderThread.h
    src/render/RenderThread.cpp
)
target_include_directories(SunsetEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SunsetEditor PRIVATE
    studio_bridge
    SunsetGraphicsEngine::core
    SunsetGraphicsEngine::schema
    MathLib::MathLib
    webview::core_static
    studio_options
)
target_compile_definitions(SunsetEditor PRIVATE
    STUDIO_VERSION="${PROJECT_VERSION}"
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# 本体 DLL / バックエンド DLL / dxcompiler.dll / dxil.dll を exe の横に配置する
sunset_gfx_deploy_runtime(SunsetEditor)

set_target_properties(SunsetEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(GNUInstallDirs)
install(TARGETS SunsetEditor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# ---- フロントエンド ----
include(StudioFrontend)
studio_add_frontend(studio_ui
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frontend"
    OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/ui"
)
if(TARGET studio_ui)
    add_dependencies(SunsetEditor studio_ui)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/ui" DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
