# ============================================================
#  studio_options
#  SunsetStudio のターゲットに共通のコンパイルオプション。各ターゲットに PRIVATE でリンクする。
#  (依存ライブラリには適用しない)
# ============================================================
add_library(studio_options INTERFACE)

target_compile_options(studio_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/WX>
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
)

# エンジンのデバッグレイヤーを使うかどうか (エンジン側の GFX_ENABLE_VALIDATION に合わせる)
target_compile_definitions(studio_options INTERFACE
    $<$<BOOL:${GFX_ENABLE_VALIDATION}>:STUDIO_GFX_VALIDATION=1>
)
