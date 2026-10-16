# SunsetStudio

[SunsetGraphicsEngine](../SunsetGraphicsEngine)(描画)と [SunsetMathLib](https://github.com/SunsetTwilight/SunsetMathLib)(数学)を組み合わせるアプリケーションです。最初のアプリは描画エディター(`SunsetEditor`)です。

どちらのライブラリも単独で使えるように作られています。このリポジトリはそれらを使う側で、ライブラリのコードは含みません。

> **現在の状態**: `SunsetEditor` は、依存ライブラリの取得・リンク・DLL の配置を確認するためのスタブです。エディター本体の設計は SunsetGraphicsEngine の [docs/design/editor-architecture.md](../SunsetGraphicsEngine/docs/design/editor-architecture.md)(Phase 4)を参照してください。

---

## ディレクトリ構成

```
SunsetStudio/
├── CMakeLists.txt              ← ルート
├── CMakePresets.json           ← 通常用 (GitHub から取得) / 開発用 (*-dev: 隣のフォルダを使用)
├── cmake/
│   ├── StudioDependencies.cmake ← 依存ライブラリの取得 (バージョンの固定はここだけ)
│   └── StudioOptions.cmake      ← 共通の警告・定義
├── editor/                     ← SunsetEditor
├── tests/                      ← 結合テスト (doctest)
└── .github/workflows/ci.yml    ← GitHub Actions
```

想定しているワークスペースの配置:

```
SunsetGraphicsEngineProject/
├── SunsetGraphicsEngine/       ← ライブラリ
├── SunsetMathLib/              ← ライブラリ
└── SunsetStudio/               ← このリポジトリ
```

---

## 依存ライブラリ

| ライブラリ | 使うターゲット | 取得方法 |
|-----------|---------------|---------|
| SunsetGraphicsEngine | `SunsetGraphicsEngine::core`、`::schema`、`sunset_gfx_deploy_runtime()` | install 済みなら `find_package`、なければ GitHub のアーカイブを取得 |
| SunsetMathLib | `MathLib::MathLib` | install 済みなら `find_package`、なければ GitHub のアーカイブを取得 |
| Dear ImGui (docking) | `studio_imgui` | アーカイブを URL で取得 (SHA256 で固定) |
| imnodes | `studio_imnodes` | アーカイブを URL で取得 (SHA256 で固定) |
| nlohmann/json | `nlohmann_json::nlohmann_json` | アーカイブを URL で取得 (SHA256 で固定) |

エンジンの DLL(`SunsetGraphicsEngine.dll`、`gfx_dx12.dll`、`dxcompiler.dll`)は、`sunset_gfx_deploy_runtime()` がビルド時に exe の横にコピーします。

---

## ビルド

前提: Visual Studio 2022 (C++ デスクトップ開発)、CMake 3.24 以上、Ninja、DXC (`C:/dxc_2025_05_24`。変更する場合は `GFX_DXC_DIR`)。

### 開発用 (隣のフォルダのライブラリを使う) — 通常はこちら

ライブラリとアプリを同時に編集できます。ライブラリの変更は、アプリをビルドし直すだけで反映されます。

```bat
cmake --preset windows-dev-debug
cmake --build --preset windows-dev-debug
build\windows-dev-debug\bin\SunsetEditor.exe
```

`FETCHCONTENT_SOURCE_DIR_SUNSETGRAPHICSENGINE` と `FETCHCONTENT_SOURCE_DIR_SUNSETMATHLIB` で、`../SunsetGraphicsEngine` と `../SunsetMathLib` を指定しています。

### 通常用 (GitHub から取得する)

```bat
cmake --preset windows-debug
cmake --build --preset windows-debug
```

`https://github.com/SunsetTwilight/<リポジトリ>/archive/<タグ>.tar.gz` を取得します。取得するバージョンは次の変数で変更できます。

| 変数 | 既定値 | 内容 |
|------|--------|------|
| `STUDIO_GFX_TAG` | `v0.1.0` | SunsetGraphicsEngine のタグ / ブランチ / コミット |
| `STUDIO_MATHLIB_TAG` | `v0.1.0` | SunsetMathLib のタグ / ブランチ / コミット |
| `STUDIO_GITHUB_OWNER` | `SunsetTwilight` | GitHub のオーナー |
| `STUDIO_GFX_URL` / `STUDIO_MATHLIB_URL` | 上から組み立てた URL | 取得元を直接指定する場合 |

既定はリリースのタグです。ライブラリの最新 (`main`) を試すときは `-DSTUDIO_GFX_TAG=main` のように指定します。ブランチを指定した場合、URL が変わらないので、2 回目以降の構成では再ダウンロードされません。最新を取り込むには `--fresh` で構成し直してください。

### install 済みのライブラリを使う

`CMAKE_PREFIX_PATH` にインストール先を指定すると、ダウンロードせずに `find_package` で見つけたパッケージを使います。

```bat
cmake --preset windows-release -DCMAKE_PREFIX_PATH=C:/path/to/prefix
```

---

## 自動テスト

`tests/` に、両ライブラリの結合テストがあります (doctest + CTest)。エンジンの DLL の配置と初期化、MathLib、スキーマを確認します。

```bat
ctest --test-dir build\windows-dev-debug --output-on-failure
```

GPU のない環境では `set GFX_TEST_ADAPTER=software` で WARP を使います。GitHub Actions (`.github/workflows/ci.yml`) では、GitHub から取得したライブラリで Debug / Release のビルドとテストを WARP で実行します。

---

## 注意点

- **git clone を使わない理由**: 所有者を記録しないドライブ (exFAT や一部のネットワークドライブなど) では、FetchContent の `git clone` が "dubious ownership" で失敗します。そのため、依存ライブラリはすべて GitHub のアーカイブで取得しています。リポジトリを Private にすると認証なしではアーカイブを取得できないので、その場合は開発用のプリセットか install 済みのパッケージを使ってください。
- **AVX2**: SunsetMathLib は既定で `/arch:AVX2` を利用側にも付けます (`MATHLIB_ENABLE_AVX2`)。そのため、SunsetEditor は AVX2 に対応した CPU でしか動きません。
