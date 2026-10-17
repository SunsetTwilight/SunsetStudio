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
#    webview        webview/webview (MIT)  … OS の WebView (Windows: WebView2) でエディターの UI を表示する
#                   Windows では configure 時に NuGet から WebView2 SDK のヘッダーを取得する
#    nlohmann_json  nlohmann/json (MIT)    … C++ と JS の間の JSON、アセットの JSON
#  フロントエンド (React / Vite など) は editor/frontend/package.json で管理する。
# ============================================================
include(FetchContent)

# ---- 自作ライブラリ ----
# GitHub のアーカイブ (archive/<タグ / ブランチ / コミット>.tar.gz) を取得する。
# git clone は、所有者を記録しないドライブで "dubious ownership" として失敗するため使わない。
# 既定はリリースのタグ (再現性のため)。ライブラリの最新を試すときは "main" を指定する。
# ※ ブランチを指定した場合、URL が変わらないので 2 回目以降は再ダウンロードされない。
#    最新を取り込むには build/<preset>/_deps を削除するか --fresh で構成し直す。
set(STUDIO_GITHUB_OWNER "SunsetTwilight" CACHE STRING "自作ライブラリの GitHub のオーナー")
set(STUDIO_GFX_TAG      "v0.2.0"         CACHE STRING "SunsetGraphicsEngine のタグ / ブランチ / コミット")
set(STUDIO_MATHLIB_TAG  "v0.1.0"         CACHE STRING "SunsetMathLib のタグ / ブランチ / コミット")
set(STUDIO_GFX_URL      "https://github.com/${STUDIO_GITHUB_OWNER}/SunsetGraphicsEngine/archive/${STUDIO_GFX_TAG}.tar.gz"
    CACHE STRING "SunsetGraphicsEngine の取得元 (アーカイブの URL)")
set(STUDIO_MATHLIB_URL  "https://github.com/${STUDIO_GITHUB_OWNER}/SunsetMathLib/archive/${STUDIO_MATHLIB_TAG}.tar.gz"
    CACHE STRING "SunsetMathLib の取得元 (アーカイブの URL)")

FetchContent_Declare(SunsetGraphicsEngine
    URL              ${STUDIO_GFX_URL}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.2
)
FetchContent_Declare(SunsetMathLib
    URL              ${STUDIO_MATHLIB_URL}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.1 NAMES MathLib
)

# ---- 外部ライブラリ ----
set(STUDIO_WEBVIEW_VERSION "0.12.0")
set(STUDIO_JSON_VERSION    "v3.12.0")

# 外部ライブラリは git ではなく GitHub のアーカイブを URL で取得し、SHA256 で固定する。
# (git clone は、所有者を記録しないドライブで "dubious ownership" として失敗することがあるため)
# バージョンを変えるときは URL_HASH も更新すること。
FetchContent_Declare(webview
    URL      https://github.com/webview/webview/archive/refs/tags/${STUDIO_WEBVIEW_VERSION}.tar.gz
    URL_HASH SHA256=e2c8d0bed3fcd13e624074448b7d3b1d0b0d03546a15839f17ee0013f2cba1db
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/${STUDIO_JSON_VERSION}/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# webview は静的ライブラリ (webview::core_static) だけを使う。テスト / サンプル / ドキュメント / インストールは不要
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_STATIC_LIBRARY ON  CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_TESTS          OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_DOCS           OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_DOCS         OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_TARGETS      OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_CHECKS        OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_PACKAGING     OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SunsetGraphicsEngine SunsetMathLib webview nlohmann_json)

# webview のソースは UTF-8 (日本語の Windows の既定のコードページでは C4819 の警告になる)
if(MSVC AND TARGET webview_core_static)
    target_compile_options(webview_core_static PRIVATE /utf-8)
endif()

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
message(STATUS "[SunsetStudio] webview ${STUDIO_WEBVIEW_VERSION} / nlohmann_json ${STUDIO_JSON_VERSION}")
