# SunsetStudio

[SunsetGraphicsEngine](../SunsetGraphicsEngine)(描画)と [SunsetMathLib](https://github.com/SunsetTwilight/SunsetMathLib)(数学)を組み合わせるアプリケーションです。最初のアプリは描画エディター(`SunsetEditor`)です。

どちらのライブラリも単独で使えるように作られています。このリポジトリはそれらを使う側で、ライブラリのコードは含みません。

> **現在の状態**: M2c(インスペクター)。`SunsetEditor` は webview のウィンドウに Web UI(React、ドッキングできるパネル)を表示し、ビューポートのパネルにエンジンの描画(ネイティブの子ウィンドウ)を重ねます。インスペクターでは、ビューポートのパイプラインの記述子を D3D12 の構造体のメンバー単位で編集でき、変更はすぐ PSO の作り直しとして反映されます。エンジンのメッセージはログのパネルに、フレームの統計はステータスバーに出ます。
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
│   │   ├── services/SchemaJson.* ← スキーマと JSON の橋渡し (インスペクター用)
│   │   ├── render/RenderThread.* ← エンジンを持つ描画スレッド (ビューポートのフレームのループ)
│   │   ├── render/ViewportRenderer.* ← ビューポートをレンダーグラフで描く
│   │   └── viewport/ViewportWindow* ← ビューポートの子ウィンドウとマウスの入力
│   ├── templates/deferred/     ← プロジェクトのテンプレート (graphs/ pipelines/ shaders/)
│   └── frontend/               ← UI (React + TypeScript + Vite + dockview)
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
| React / Vite / TypeScript / dockview など | MIT / Apache-2.0 | `editor/frontend/package.json` | npm (`package-lock.json` で固定) |

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
| `STUDIO_GFX_TAG` | `v0.2.1` | SunsetGraphicsEngine のタグ / ブランチ / コミット |
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
build\windows-dev-debug\bin\SunsetEditor.exe D:\Work\MyGame\MyGame.sunsetproj
```

プロジェクト (`.sunsetproj`) を指定しないと起動画面が出ます。最近のプロジェクトから選ぶか、「開く…」で選ぶか、
テンプレートから新しいプロジェクトを作ります。プロジェクトは次の形です (詳しくは docs/design/editor-shell.md の「プロジェクトファイル」)。

```
MyGame/
├── MyGame.sunsetproj   ← プロジェクトファイル (JSON)
├── Assets/             ← グラフ、パイプライン、シェーダー (git で管理する)
└── Saved/              ← パネルの配置など (git で管理しない)
```

| 指定 | 内容 |
|------|------|
| `<ファイル>.sunsetproj` | そのプロジェクトを開いて起動する |
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

ビューポートの操作: 左ドラッグで回転、中 / 右ドラッグで平行移動、ホイールでズーム、F キーで視点を戻す。
開いたプロジェクトの `Assets/` のファイル (グラフ `graphs/viewport.json`、パイプライン `pipelines/*.json`、シェーダー `shaders/*.hlsl`) を保存すると、
実行中のビューポートにその場で反映されます。
エディターの「レンダーグラフ」パネルとインスペクターで、パスの有効 / 無効、つなぎ方、リソースのフォーマット、
パイプラインのステートを編集して保存することもできます。
編集はツールバーの「元に戻す」「やり直す」(Ctrl+Z / Ctrl+Y) で取り消せます。
「ノードエディター」では、パスとリソースをノードと線で表示し、ドラッグで並べ、ハンドルどうしをつないで
入力 / 出力を加え、Delete で消し、右クリックでパスとリソースを加えられます。
ツールバーの「リソースビューアー」で、グラフの途中のテクスチャ (G-Buffer、深度など) をビューポートに表示し、
カーソルの下のピクセルの値を確かめられます。
ノードエディターでノードを右クリックすると、そのノードの結果 (リソースならそれ自身、パスなら出力 / 入力のテクスチャ) だけを
ビューポートに表示するか、タブ (パネル) または別ウィンドウに表示できます。ツールバーの「表示中: … ✕」で最終出力に戻します。
パネルのタブをウィンドウの外へドラッグして離すと、そのパネルは別ウィンドウになります
(ビューポートはツールバーの「ビューポートを別ウィンドウに」でも切り離せ、閉じると元に戻ります)。
インスペクターでは、パイプラインのアセットを選び、ブレンド、ラスタライザー、深度ステンシル、入力レイアウトなどの値を変えると、すぐビューポートに反映されます。
既定値と違うフィールドは名前が強調され、不正な値や PSO を作れない組み合わせはエラーとして表示されます (エンジン側は元のまま)。

パネルはタブをドラッグして移動 / 分割できます。ビューポートの上には Web の UI を重ねられないため、ドラッグ中はビューポートを隠します。

C++ と JS の間のメソッドとイベントの型は `editor/frontend/src/bridge/protocol.ts`、C++ 側の登録は `editor/src/app/EditorApp.cpp` にあります。

---

## 自動テスト

| テスト | 内容 | 必要なもの |
|-------|------|-----------|
| `integration` | エンジンの DLL の配置と初期化、MathLib、スキーマ | GPU または WARP |
| `unit` | Bridge (RPC) と、スキーマと JSON の橋渡しの単体テスト | なし |
| `editor_smoke` | `SunsetEditor --smoke-test --software`。ウィンドウを開き、UI の `ui.ready` / `app.info`、エンジンの初期化、ビューポートの描画、テンプレートから一時フォルダに作ったプロジェクト、インスペクターの経路 (`pipeline.update`)、レンダーグラフの経路 (`graph.set`、元に戻す / やり直す、プロジェクトへの `graph.save`)、リソースビューアーのピクセルの値、別ウィンドウ (リソースのプレビューと、ビューポートの切り離し / 戻し) を確認し、D3D12 のメッセージが 0 件であることを確認する | WebView2 ランタイム、デスクトップ (ラベル `gui`) |

```bat
ctest --test-dir build\windows-dev-debug --output-on-failure
```

GPU のない環境では `set GFX_TEST_ADAPTER=software` で WARP を使います。GitHub Actions (`.github/workflows/ci.yml`) では、GitHub から取得したライブラリで Debug / Release のビルドとテストを WARP で実行します。スモークテストも必須です (GitHub の windows-2022 のランナーでも WebView2 のウィンドウを開けます)。

---

## 注意点

- **git clone を使わない理由**: 所有者を記録しないドライブ (exFAT や一部のネットワークドライブなど) では、FetchContent の `git clone` が "dubious ownership" で失敗します。そのため、依存ライブラリはすべて GitHub のアーカイブで取得しています。リポジトリを Private にすると認証なしではアーカイブを取得できないので、その場合は開発用のプリセットか install 済みのパッケージを使ってください。
- **AVX2**: SunsetMathLib は既定で `/arch:AVX2` を利用側にも付けます (`MATHLIB_ENABLE_AVX2`)。そのため、SunsetEditor は AVX2 に対応した CPU でしか動きません。
- **node_modules**: フロントエンドの依存パッケージは `editor/frontend/node_modules` に入ります (git の管理外)。
