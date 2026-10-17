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

# ---- スキーマと Bridge の JSON の橋渡し、プロジェクトファイル (GPU / webview に依存しない) ----
add_library(studio_services STATIC
    src/services/SchemaJson.h
    src/services/SchemaJson.cpp
    src/services/Project.h            # プロジェクトファイル (.sunsetproj) と最近のプロジェクト
    src/services/Project.cpp
    src/services/UndoStack.h          # 編集の履歴 (Undo / Redo)
    src/services/UndoStack.cpp
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
    src/app/EditorWindows.cpp
    src/host/WebHost.h
    src/host/WebHost.cpp
    src/host/FileDialog.h
    src/host/FileDialog_win32.cpp
    src/render/RenderThread.h
    src/render/RenderThread.cpp
    src/render/OrbitCamera.h
    src/render/ViewportRenderer.h
    src/render/ViewportRenderer.cpp
    src/render/ResourceViewer.h
    src/render/ResourceViewer.cpp
    src/viewport/ViewportWindow.h
    src/viewport/ViewportWindow_win32.cpp
    src/viewport/FloatingWindow.h
    src/viewport/FloatingWindow_win32.cpp
)
target_include_directories(SunsetEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SunsetEditor PRIVATE
    studio_bridge
    studio_services
    SunsetGraphicsEngine::core
    SunsetGraphicsEngine::schema
    SunsetGraphicsEngine::rendergraph
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

# ---- プロジェクトのテンプレート (M4a) ----
# editor/templates/<名前>/ の中身が、新しいプロジェクトの Assets/ になる (graphs/ pipelines/ shaders/)。
# bin/templates にコピーする (配布用)。ビルドツリーの exe はソースのフォルダを使う
file(GLOB_RECURSE STUDIO_EDITOR_TEMPLATES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/*.json" "${CMAKE_CURRENT_SOURCE_DIR}/templates/*.hlsl*")
target_sources(SunsetEditor PRIVATE ${STUDIO_EDITOR_TEMPLATES})
set_source_files_properties(${STUDIO_EDITOR_TEMPLATES} PROPERTIES HEADER_FILE_ONLY TRUE)
add_custom_target(studio_editor_templates ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/templates" "${CMAKE_BINARY_DIR}/bin/templates"
    COMMENT "Copying project templates to bin/templates"
    VERBATIM
)
add_dependencies(SunsetEditor studio_editor_templates)
# エンジンのバージョン ("0.3")。プロジェクトファイルの engine に書き、開くときに比べる。
# 実際に使っているエンジンから取る: install 済みのパッケージなら find_package の結果、
# ソース (FetchContent / 開発用のローカルのフォルダ) ならその CMakeLists.txt の project(VERSION)。
# (STUDIO_GFX_TAG はキャッシュに古い値が残ることがあるので使わない)
set(_studio_gfx_full "${SunsetGraphicsEngine_VERSION}")
if(NOT _studio_gfx_full)
    FetchContent_GetProperties(SunsetGraphicsEngine SOURCE_DIR _studio_gfx_src)
    if(_studio_gfx_src AND EXISTS "${_studio_gfx_src}/CMakeLists.txt")
        file(STRINGS "${_studio_gfx_src}/CMakeLists.txt" _studio_gfx_project REGEX "project\\(SunsetGraphicsEngine VERSION")
        if(_studio_gfx_project MATCHES "VERSION ([0-9.]+)")
            set(_studio_gfx_full "${CMAKE_MATCH_1}")
        endif()
    endif()
endif()
set(STUDIO_GFX_VERSION "")
if(_studio_gfx_full MATCHES "^([0-9]+)\\.([0-9]+)")
    set(STUDIO_GFX_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")
endif()
message(STATUS "[SunsetStudio] エンジンのバージョン (プロジェクトファイル用): ${STUDIO_GFX_VERSION}")
target_compile_definitions(SunsetEditor PRIVATE
    "STUDIO_TEMPLATE_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/templates\""
    "STUDIO_GFX_VERSION=\"${STUDIO_GFX_VERSION}\"")

# 本体 DLL / バックエンド DLL / dxcompiler.dll / dxil.dll を exe の横に配置する
sunset_gfx_deploy_runtime(SunsetEditor)

set_target_properties(SunsetEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(GNUInstallDirs)
install(TARGETS SunsetEditor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY templates DESTINATION "${CMAKE_INSTALL_BINDIR}")

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
