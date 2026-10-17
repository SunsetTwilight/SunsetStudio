# ============================================================
#  SunsetEditor
#
#  設計: docs/design/editor-shell.md
#    UI      : webview/webview + Web UI (editor/frontend、React + TypeScript + Vite)
#    描画    : SunsetGraphicsEngine (描画スレッドが持つ)
#
#  ターゲット:
#    studio_bridge   C++ と JS の RPC (webview に依存しない。単体テストする)
#    studio_services スキーマと JSON の橋渡し (インスペクター用。単体テストする)
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

# ---- スキーマと Bridge の JSON の橋渡し (インスペクター用。GPU / webview に依存しない) ----
add_library(studio_services STATIC
    src/services/SchemaJson.h
    src/services/SchemaJson.cpp
)
target_include_directories(studio_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(studio_services
    PUBLIC  SunsetGraphicsEngine::schema nlohmann_json::nlohmann_json
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
    src/render/OrbitCamera.h
    src/render/SceneRenderer.h
    src/render/SceneRenderer.cpp
    src/viewport/ViewportWindow.h
    src/viewport/ViewportWindow_win32.cpp
)
target_include_directories(SunsetEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SunsetEditor PRIVATE
    studio_bridge
    studio_services
    SunsetGraphicsEngine::core
    SunsetGraphicsEngine::schema
    MathLib::MathLib
    webview::core_static
    comctl32            # SetWindowSubclass (閉じる前にスワップチェーンを解放する)
    studio_options
)
target_compile_definitions(SunsetEditor PRIVATE
    STUDIO_VERSION="${PROJECT_VERSION}"
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ---- ビューポートのシェーダー ----
# editor/shaders を bin/shaders にコピーする (配布用)。ビルドツリーの exe はソースのフォルダを直接読むので、
# 保存するとその場で反映される (ShaderLibrary のホットリロード)
file(GLOB STUDIO_EDITOR_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsl*")
target_sources(SunsetEditor PRIVATE ${STUDIO_EDITOR_SHADERS})
set_source_files_properties(${STUDIO_EDITOR_SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE)
add_custom_target(studio_editor_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${CMAKE_BINARY_DIR}/bin/shaders"
    COMMENT "Copying editor shaders to bin/shaders"
    VERBATIM
)
add_dependencies(SunsetEditor studio_editor_shaders)
target_compile_definitions(SunsetEditor PRIVATE
    "STUDIO_SHADER_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/shaders\"")

# 本体 DLL / バックエンド DLL / dxcompiler.dll / dxil.dll を exe の横に配置する
sunset_gfx_deploy_runtime(SunsetEditor)

set_target_properties(SunsetEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(GNUInstallDirs)
install(TARGETS SunsetEditor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY shaders DESTINATION "${CMAKE_INSTALL_BINDIR}")

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
