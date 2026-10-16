# ============================================================
#  依存ライブラリ (FetchContent)
#
#  バージョンはタグ / ブランチ / コミットで固定する。更新するときはここだけを書き換える。
#
#  自作ライブラリ:
#    SunsetGraphicsEngine → SunsetGraphicsEngine::core / ::schema、sunset_gfx_deploy_runtime()
#    SunsetMathLib        → MathLib::MathLib
#    どちらも install 済みのパッケージがあれば find_package で優先して使う (FIND_PACKAGE_ARGS)。
#    開発時は FETCHCONTENT_SOURCE_DIR_SUNSETGRAPHICSENGINE / FETCHCONTENT_SOURCE_DIR_SUNSETMATHLIB に
#    ローカルのフォルダを指定すると、ダウンロードせずにそれを使う (CMakePresets.json の *-dev)。
#
#  外部ライブラリ (エディター用):
#    studio_imgui   Dear ImGui (docking ブランチ)   … UI
#    studio_imnodes imnodes                        … レンダーグラフのノードエディター
#    nlohmann_json  nlohmann/json                  … グラフ / パイプラインアセットの JSON
# ============================================================
include(FetchContent)

# ---- 自作ライブラリ ----
set(STUDIO_GFX_REPOSITORY  "https://github.com/SunsetTwilight/SunsetGraphicsEngine.git" CACHE STRING "SunsetGraphicsEngine の取得元")
set(STUDIO_GFX_TAG         "main"                                                    CACHE STRING "SunsetGraphicsEngine のタグ / ブランチ")
set(STUDIO_MATHLIB_REPOSITORY "https://github.com/SunsetTwilight/SunsetMathLib.git"  CACHE STRING "SunsetMathLib の取得元")
set(STUDIO_MATHLIB_TAG     "main"                                                    CACHE STRING "SunsetMathLib のタグ / ブランチ")

FetchContent_Declare(SunsetGraphicsEngine
    GIT_REPOSITORY   ${STUDIO_GFX_REPOSITORY}
    GIT_TAG          ${STUDIO_GFX_TAG}
    FIND_PACKAGE_ARGS 1
)
FetchContent_Declare(SunsetMathLib
    GIT_REPOSITORY   ${STUDIO_MATHLIB_REPOSITORY}
    GIT_TAG          ${STUDIO_MATHLIB_TAG}
    FIND_PACKAGE_ARGS NAMES MathLib
)

# ---- 外部ライブラリ ----
set(STUDIO_IMGUI_TAG    "v1.92.9-docking")
set(STUDIO_IMNODES_TAG  "eb36902c892548ef94f88f51ad7e7c9c7058a71c")   # master (タグ v0.5 は古い ImGui 向け)
set(STUDIO_JSON_VERSION "v3.12.0")

# 外部ライブラリは git ではなく GitHub のアーカイブを URL で取得し、SHA256 で固定する。
# (git clone は、所有者を記録しないドライブで "dubious ownership" として失敗することがあるため)
# バージョンを変えるときは URL_HASH も更新すること。
#
# ImGui / imnodes は同梱の CMake を使わない (ImGui は持たず、imnodes のものは find_package(imgui) 前提)。
# SOURCE_SUBDIR に存在しないディレクトリを指定して、ソースの取得だけを行い、ターゲットは下で自前で作る。
FetchContent_Declare(imgui
    URL      https://github.com/ocornut/imgui/archive/refs/tags/${STUDIO_IMGUI_TAG}.tar.gz
    URL_HASH SHA256=61f2ec7ef19702e47689b77cc602e3aeeda423941bfbc5d8ecb6a02d05a9dd73
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR  _studio_no_cmake_
)
FetchContent_Declare(imnodes
    URL      https://github.com/Nelarius/imnodes/archive/${STUDIO_IMNODES_TAG}.tar.gz
    URL_HASH SHA256=39bb2a97c2e42b63da2140abe7c3038eb037a30194f50ef681ee2e57357b0a21
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR  _studio_no_cmake_
)
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/${STUDIO_JSON_VERSION}/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SunsetGraphicsEngine SunsetMathLib imgui imnodes nlohmann_json)

# ---- Dear ImGui (コアのみ。描画はエンジンの ABI の上に実装するため、プラットフォーム / レンダラーのバックエンドは使わない) ----
add_library(studio_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(studio_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
target_compile_definitions(studio_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# ---- imnodes ----
add_library(studio_imnodes STATIC ${imnodes_SOURCE_DIR}/imnodes.cpp)
target_include_directories(studio_imnodes SYSTEM PUBLIC "${imnodes_SOURCE_DIR}")
target_link_libraries(studio_imnodes PUBLIC studio_imgui)

# ---- どこから取得したかを表示する ----
# install 済みのパッケージから来たターゲットは IMPORTED になる
foreach(_pair "SunsetGraphicsEngine|SunsetGraphicsEngine::core" "SunsetMathLib|MathLib::MathLib")
    string(REPLACE "|" ";" _pair "${_pair}")
    list(GET _pair 0 _dep)
    list(GET _pair 1 _tgt)
    string(TOUPPER ${_dep} _DEP)
    get_target_property(_imported ${_tgt} IMPORTED)
    if(FETCHCONTENT_SOURCE_DIR_${_DEP})
        message(STATUS "[SunsetStudio] ${_dep}: ローカル ${FETCHCONTENT_SOURCE_DIR_${_DEP}}")
    elseif(_imported)
        message(STATUS "[SunsetStudio] ${_dep}: install 済みのパッケージ (find_package)")
    else()
        message(STATUS "[SunsetStudio] ${_dep}: FetchContent で取得")
    endif()
endforeach()
message(STATUS "[SunsetStudio] imgui ${STUDIO_IMGUI_TAG} / imnodes ${STUDIO_IMNODES_TAG} / nlohmann_json ${STUDIO_JSON_VERSION}")
