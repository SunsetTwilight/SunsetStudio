# ============================================================
#  studio_add_frontend(<target> SOURCE_DIR <dir> OUTPUT_DIR <dir>)
#
#  Vite のフロントエンドをビルドし、1 つにまとめた index.html を OUTPUT_DIR に置く。
#    1. package-lock.json が変わったら npm ci (node_modules はソースのフォルダに作る)
#    2. ソースが変わったら npm run build -- --outDir <OUTPUT_DIR>
#
#  オプション STUDIO_BUILD_UI (既定 ON)。OFF のときはターゲットを作らない
#  (UI は STUDIO_UI_DEV_URL の開発サーバーか、別に用意した index.html を使う)。
#  Node.js (npm) が見つからなければ configure をエラーにする。
# ============================================================
include_guard(GLOBAL)

option(STUDIO_BUILD_UI "エディターのフロントエンド (editor/frontend) を npm でビルドする。Node.js が必要" ON)

function(studio_add_frontend target)
    cmake_parse_arguments(ARG "" "SOURCE_DIR;OUTPUT_DIR" "" ${ARGN})
    if(NOT STUDIO_BUILD_UI)
        message(STATUS "[SunsetStudio] フロントエンド: ビルドしない (STUDIO_BUILD_UI=OFF)")
        return()
    endif()

    find_program(STUDIO_NPM NAMES npm.cmd npm)
    if(NOT STUDIO_NPM)
        message(FATAL_ERROR "[SunsetStudio] npm が見つかりません。Node.js (LTS) をインストールするか、"
            "-DSTUDIO_BUILD_UI=OFF で構成してください (https://nodejs.org/)")
    endif()
    execute_process(COMMAND "${STUDIO_NPM}" --version OUTPUT_VARIABLE _npm_version OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "[SunsetStudio] フロントエンド: npm ${_npm_version} (${STUDIO_NPM})")

    set(_src "${ARG_SOURCE_DIR}")
    set(_installed "${_src}/node_modules/.package-lock.json")   # npm ci が書き出す

    # 1. 依存パッケージ (package-lock.json のとおり)
    add_custom_command(
        OUTPUT  "${_installed}"
        COMMAND "${STUDIO_NPM}" ci --no-audit --no-fund
        DEPENDS "${_src}/package.json" "${_src}/package-lock.json"
        WORKING_DIRECTORY "${_src}"
        COMMENT "npm ci (editor/frontend)"
        VERBATIM
    )

    # 2. ビルド
    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
        "${_src}/src/*.ts" "${_src}/src/*.tsx" "${_src}/src/*.css")
    set(_out "${ARG_OUTPUT_DIR}/index.html")
    add_custom_command(
        OUTPUT  "${_out}"
        COMMAND "${STUDIO_NPM}" run build -- --outDir "${ARG_OUTPUT_DIR}" --emptyOutDir
        DEPENDS "${_installed}" ${_sources}
                "${_src}/index.html" "${_src}/vite.config.ts" "${_src}/tsconfig.json"
        WORKING_DIRECTORY "${_src}"
        COMMENT "npm run build (editor/frontend → ${ARG_OUTPUT_DIR})"
        VERBATIM
    )
    add_custom_target(${target} ALL DEPENDS "${_out}" SOURCES ${_sources})
endfunction()
