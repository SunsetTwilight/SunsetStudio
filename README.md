# SunsetStudio

[SunsetGraphicsEngine](../SunsetGraphicsEngine)(描画)と [SunsetMathLib](https://github.com/SunsetTwilight/SunsetMathLib)(数学)を組み合わせるアプリケーションです。最初のアプリは描画エディター(`SunsetEditor`)です。

どちらのライブラリも単独で使えるように作られています。このリポジトリはそれらを使う側で、ライブラリのコードは含みません。

> **現在の状態**: M2a(エディターの骨格)。`SunsetEditor` は webview のウィンドウに Web UI(React)を表示し、描画スレッドでエンジンを初期化して、エンジンのメッセージを UI のログに表示します。3D ビューポートは M2b、インスペクターは M2c で入ります。
> 設計: [docs/design/editor-shell.md](docs/design/editor-shell.md)(エディターの骨格)、SunsetGraphicsEngine の [docs/design/editor-architecture.md](../SunsetGraphicsEngine/docs/design/editor-architecture.md)(全体)

---

## ディレクトリ構成

```
SunsetStudio/
├── CMakeLists.txt              ← ルート
├── CMakePresets.json           ← 通常用 (GitHub から取得) / 開発用 (*-dev: 隣のフォルダを使用)
├── cmake/
│   ├── StudioDependencies.cmake ← 依存ライブラリの取得 (バージョンの固定はここだけ)
│   ├── StudioFrontend.cmake     ← フロントエンドのビルド (npm)
│   └── StudioOptions.cmake      ← 共通の警告・定義
├── editor/                     ← SunsetEditor
│   ├── src/
│   │   ├── main.cpp
│   │   ├── app/EditorApp.*      ← 全体の寿命、JS から呼べるメソッドとイベント
│   │   ├── host/WebHost.*       ← webview の包み (UI スレッド)
│   │   ├── host/Bridge.*        ← JS → C++ の RPC (webview に依存しない)
│   │   └── render/RenderThread.* ← エンジンを持つ描画スレッド
│   └── frontend/               ← UI (React + TypeScript + Vite)
├── tests/                      ← 結合テスト、Bridge の単体テスト、エディターのスモークテスト (doctest)
├── docs/design/                ← 設計ドキュメント
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

| ライブラリ | ライセンス | 使うターゲット / 場所 | 取得方法 |
|-----------|-----------|---------------------|---------|
| SunsetGraphicsEngine | — | `SunsetGraphicsEngine::core`、`::schema`、`sunset_gfx_deploy_runtime()` | install 済みなら `find_package`、なければ GitHub のアーカイブを取得 |
| SunsetMathLib | — | `MathLib::MathLib` | 同上 |
| webview/webview 0.12.0 | MIT | `webview::core_static` | アーカイブを URL で取得 (SHA256 で固定)。Windows では configure 時に NuGet から WebView2 SDK のヘッダーを取得する |
| nlohmann/json 3.12.0 | MIT | `nlohmann_json::nlohmann_json` | アーカイブを URL で取得 (SHA256 で固定) |
| React / Vite / TypeScript など | MIT / Apache-2.0 | `editor/frontend/package.json` | npm (`package-lock.json` で固定) |

エンジンの DLL(`SunsetGraphicsEngine.dll`、`gfx_dx12.dll`、`dxcompiler.dll`、`dxil.dll`)は、`sunset_gfx_deploy_runtime()` がビルド時に exe の横にコピーします。DXC はエンジンが configure 時に取得します。

---

## ビルド

前提:

- Visual Studio 2022 (C++ デスクトップ開発)、CMake 3.24 以上、Ninja
- Node.js (LTS。`editor/frontend/.nvmrc`)。フロントエンドのビルドに使います。不要なら `-DSTUDIO_BUILD_UI=OFF`
- 実行時: Microsoft Edge WebView2 ランタイム (Windows 11 には標準で入っています)

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
| `STUDIO_GFX_TAG` | `v0.2.0` | SunsetGraphicsEngine のタグ / ブランチ / コミット |
| `STUDIO_MATHLIB_TAG` | `v0.1.0` | SunsetMathLib のタグ / ブランチ / コミット |
| `STUDIO_GITHUB_OWNER` | `SunsetTwilight` | GitHub のオーナー |
| `STUDIO_GFX_URL` / `STUDIO_MATHLIB_URL` | 上から組み立てた URL | 取得元を直接指定する場合 |
| `STUDIO_BUILD_UI` | `ON` | フロントエンドを npm でビルドして `bin/ui/index.html` に置く |

既定はリリースのタグです。ライブラリの最新 (`main`) を試すときは `-DSTUDIO_GFX_TAG=main` のように指定します。ブランチを指定した場合、URL が変わらないので、2 回目以降の構成では再ダウンロードされません。最新を取り込むには `--fresh` で構成し直してください。

### install 済みのライブラリを使う

`CMAKE_PREFIX_PATH` にインストール先を指定すると、ダウンロードせずに `find_package` で見つけたパッケージを使います。

```bat
cmake --preset windows-release -DCMAKE_PREFIX_PATH=C:/path/to/prefix
```

---

## エディターの実行と UI の開発

```bat
build\windows-dev-debug\bin\SunsetEditor.exe
```

| 指定 | 内容 |
|------|------|
| `--software` / `STUDIO_ADAPTER=software` | WARP (ソフトウェアレンダラー) を使う |
| `--devtools` / `STUDIO_DEVTOOLS=1` | 開発者ツール (F12) を使えるようにする。Debug ビルドでは常に有効 |
| `STUDIO_UI_DEV_URL=http://localhost:5173` | Vite の開発サーバーの UI を開く (localhost のみ) |
| `--smoke-test` | UI とエンジンが動いたことを確認して終了する (自動テスト用。成功なら終了コード 0) |

UI をホットリロードしながら作るときは、開発サーバーを起動してからエディターを開きます。

```bat
cd editor\frontend
npm ci
npm run dev
```

```bat
set STUDIO_UI_DEV_URL=http://localhost:5173
build\windows-dev-debug\bin\SunsetEditor.exe
```

C++ と JS の間のメソッドとイベントの型は `editor/frontend/src/bridge/protocol.ts`、C++ 側の登録は `editor/src/app/EditorApp.cpp` にあります。

---

## 自動テスト

| テスト | 内容 | 必要なもの |
|-------|------|-----------|
| `integration` | エンジンの DLL の配置と初期化、MathLib、スキーマ | GPU または WARP |
| `unit` | Bridge (RPC) の単体テスト | なし |
| `editor_smoke` | `SunsetEditor --smoke-test --software`。ウィンドウを開き、UI の `ui.ready` / `app.info` とエンジンの初期化を確認する | WebView2 ランタイム、デスクトップ (ラベル `gui`) |

```bat
ctest --test-dir build\windows-dev-debug --output-on-failure
```

GPU のない環境では `set GFX_TEST_ADAPTER=software` で WARP を使います。GitHub Actions (`.github/workflows/ci.yml`) では、GitHub から取得したライブラリで Debug / Release のビルドとテストを WARP で実行します。スモークテストはランナーの環境に左右されるので、失敗しても CI を止めません。

---

## 注意点

- **git clone を使わない理由**: 所有者を記録しないドライブ (exFAT や一部のネットワークドライブなど) では、FetchContent の `git clone` が "dubious ownership" で失敗します。そのため、依存ライブラリはすべて GitHub のアーカイブで取得しています。リポジトリを Private にすると認証なしではアーカイブを取得できないので、その場合は開発用のプリセットか install 済みのパッケージを使ってください。
- **AVX2**: SunsetMathLib は既定で `/arch:AVX2` を利用側にも付けます (`MATHLIB_ENABLE_AVX2`)。そのため、SunsetEditor は AVX2 に対応した CPU でしか動きません。
- **node_modules**: フロントエンドの依存パッケージは `editor/frontend/node_modules` に入ります (git の管理外)。
