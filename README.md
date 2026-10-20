# SunsetStudio

[SunsetGraphicsEngine](../SunsetGraphicsEngine)(描画)、[SunsetMathLib](https://github.com/SunsetTwilight/SunsetMathLib)(数学)、[SunsetAsset](https://github.com/SunsetTwilight/SunsetAsset)(アセットの取り込みと管理)を組み合わせるアプリケーションです。最初のアプリは描画エディター(`SunsetEditor`)です。

> **AI を使った開発について**: このリポジトリは、AI (Anthropic の Claude / Claude Code) を使って作成しています。コード、テスト、ドキュメントに AI が書いたものを含みます。コミットのメッセージに `Co-Authored-By: Claude` が付いているものは、AI と一緒に作成したコミットです。

どのライブラリも単独で使えるように作られています。このリポジトリはそれらを使う側で、ライブラリのコードは含みません。

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
├── SunsetAsset/                ← ライブラリ
└── SunsetStudio/               ← このリポジトリ
```

---

## 依存ライブラリ

| ライブラリ | ライセンス | 使うターゲット / 場所 | 取得方法 |
|-----------|-----------|---------------------|---------|
| SunsetGraphicsEngine | — | `SunsetGraphicsEngine::core`、`::schema`、`sunset_gfx_deploy_runtime()` | install 済みなら `find_package`、なければ GitHub のアーカイブを取得 |
| SunsetMathLib | — | `MathLib::MathLib` | 同上 |
| SunsetAsset | — | `SunsetAsset::core`、`::model`、`::texture` (モデル / テクスチャの取り込み。中で ufbx / cgltf / stb_image を使う) | 同上 (タグができるまではコミットで固定) |
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

`FETCHCONTENT_SOURCE_DIR_SUNSETGRAPHICSENGINE` / `_SUNSETMATHLIB` / `_SUNSETASSET` で、`../SunsetGraphicsEngine`、`../SunsetMathLib`、`../SunsetAsset` を指定しています。
(すでに構成したビルドフォルダでは、プリセットに足した変数は `cmake --preset <名前>` で構成し直すまで効きません。)

### 通常用 (GitHub から取得する)

```bat
cmake --preset windows-debug
cmake --build --preset windows-debug
```

`https://github.com/SunsetTwilight/<リポジトリ>/archive/<タグ>.tar.gz` を取得します。取得するバージョンは次の変数で変更できます。

| 変数 | 既定値 | 内容 |
|------|--------|------|
| `STUDIO_GFX_TAG` | `v0.4.2` | SunsetGraphicsEngine のタグ / ブランチ / コミット |
| `STUDIO_MATHLIB_TAG` | `v0.1.0` | SunsetMathLib のタグ / ブランチ / コミット |
| `STUDIO_ASSET_TAG` | (コミット) | SunsetAsset のタグ / ブランチ / コミット |
| `STUDIO_GITHUB_OWNER` | `SunsetTwilight` | GitHub のオーナー |
| `STUDIO_GFX_URL` / `STUDIO_MATHLIB_URL` / `STUDIO_ASSET_URL` | 上から組み立てた URL | 取得元を直接指定する場合 |
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
MyWork/
├── MyWork.sunsetproj   ← プロジェクトファイル (JSON)
├── graphs/ pipelines/ shaders/       ← レンダーグラフ、パイプライン、シェーダー (git で管理する)
├── scenes/ materials/ lights/        ← シーン、マテリアル、ライトの種類 (M5 で使う)
├── assets/             ← モデルとテクスチャ (ライブラリのパネルで取り込む。<ファイル>.meta に ID と設定)
├── Saved/              ← パネルの配置など (git で管理しない)
└── Cache/              ← 作り直せるもの (git で管理しない)
```

以前の構成 (アセットを `Assets/` の下に置いたプロジェクト) もそのまま開けます。「ファイル > 古い構成 (Assets/) を新しい構成に移す…」(タイトルバーの「古い構成」の印からも) で、
`Assets/` の中身を直下へ移せます (直下に同じ名前のものがあれば、何も動かさずに中止します)。

| 指定 | 内容 |
|------|------|
| `<ファイル>.sunsetproj` | そのプロジェクトを開いて起動する |
| `--software` / `STUDIO_ADAPTER=software` | WARP (ソフトウェアレンダラー) を使う |
| `--devtools` / `STUDIO_DEVTOOLS=1` | 開発者ツール (F12) を使えるようにする。Debug ビルドでは常に有効 |
| `STUDIO_UI_DEV_URL=http://localhost:5173` | Vite の開発サーバーの UI を開く (localhost のみ) |
| `--native-titlebar` / `STUDIO_NATIVE_TITLEBAR=1` | OS のタイトルバーを使う (UI のタイトルバー兼メニューバーは、ウィンドウのボタンなしで出す) |
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

マテリアル (M5-4): インスペクターで物体を選ぶと、スロットごとのマテリアルが出ます。割り当てていないスロットは、取り込んだ値 (モデルの基本色とテクスチャ) の標準のマテリアルで描きます。「マテリアルにする」で `materials/*.material.json` に書き出して割り当て、マテリアルのタブで編集します (欄はシェーダーの `cbuffer Material` とテクスチャの変数から自動で作ります)。

タイトルバーはメニューバーを兼ねます (VS Code のような形。ファイル / 編集 / 表示 / ウィンドウ / ヘルプ)。何もない所をドラッグするとウィンドウが動き、ダブルクリックで最大化します。Alt でメニューをキーボードで操作でき、Alt + 文字 (Alt+F など) で開きます。

ビューポートの操作: 左ドラッグで回転、中 / 右ドラッグで平行移動、ホイールでズーム、F キーで視点を戻す。
開いたプロジェクトのファイル (グラフ `graphs/viewport.json`、パイプライン `pipelines/*.json`、シェーダー `shaders/*.hlsl`) を保存すると、
実行中のビューポートにその場で反映されます。
エディターの「レンダーグラフ」パネルとインスペクターで、パスの有効 / 無効、つなぎ方、リソースのフォーマット、
パイプラインのステートを編集して保存することもできます。
編集は「編集 > 元に戻す / やり直す」(Ctrl+Z / Ctrl+Y) で取り消せます。
「ノードエディター」では、パスとリソースをノードと線で表示し、ドラッグで並べ、ハンドルどうしをつないで
入力 / 出力を加え、Delete で消し、右クリックでパスとリソースを加えられます。
「表示 > リソースビューアー」で、グラフの途中のテクスチャ (G-Buffer、深度など) をビューポートに表示し、
カーソルの下のピクセルの値を確かめられます。
ノードエディターでノードを右クリックすると、そのノードの結果 (リソースならそれ自身、パスなら出力 / 入力のテクスチャ) だけを
ビューポートに表示するか、タブ (パネル) または別ウィンドウに表示できます。タイトルバーの「表示中: … ✕」か「表示 > 最終出力に戻す」で最終出力に戻します。
パネルのタブをウィンドウの外へドラッグして離すと、そのパネルは別ウィンドウになります
(ビューポートは「ウィンドウ > ビューポートを別ウィンドウに」でも切り離せ、閉じると元に戻ります)。
「シーン」パネルには、ビューポートに描く物体 (`scenes/main.scene.json`) が木で並びます。ビューポートをクリックするか一覧で選ぶと、
インスペクターで名前、親、位置 / 回転 (度) / 大きさ、表示、色を変えられます (元に戻す / やり直すが効き、保存はシーンのパネルの「保存」)。
「＋ 立方体」「複製」「削除」(Delete キー) で物体を増減できます。選んだ物体には、ビューポートに橙の辺の印が付きます。
パスのノードを右クリックすると、パイプラインと VS / PS / CS のシェーダーをタブで開けます (Monaco Editor、HLSL の色分け)。
Ctrl+S で保存するとコンパイルして描画に反映し、エラーはその行に印が付きます (失敗しているあいだは前のシェーダーのまま描きます)。
「VS Code」「既定のアプリ」で外部のエディターでも開け、外で保存した変更も反映されます。
「新しいシェーダーを作る」では、テンプレートからシェーダーを作り、パイプラインを複製して差し替え、そのパスにつなぎます。
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
| `editor_smoke` | `SunsetEditor --smoke-test --software`。ウィンドウを開き、UI の `ui.ready` / `app.info`、エンジンの初期化、ビューポートの描画、テンプレートから一時フォルダに作ったプロジェクト、インスペクターの経路 (`pipeline.update`)、レンダーグラフの経路 (`graph.set`、元に戻す / やり直す、プロジェクトへの `graph.save`)、リソースビューアーのピクセルの値、別ウィンドウ (リソースのプレビューと、ビューポートの切り離し / 戻し)、シェーダー編集の経路 (`shader.compile` のエラーとリフレクション、テンプレートからの作成)、シーン (既定のシーンの読み込み、中央のクリックでの選択、動かして元に戻す) を確認し、D3D12 のメッセージが 0 件であることを確認する | WebView2 ランタイム、デスクトップ (ラベル `gui`) |

```bat
ctest --test-dir build\windows-dev-debug --output-on-failure
```

GPU のない環境では `set GFX_TEST_ADAPTER=software` で WARP を使います。GitHub Actions (`.github/workflows/ci.yml`) では、GitHub から取得したライブラリで Debug / Release のビルドとテストを WARP で実行します。スモークテストも必須です (GitHub の windows-2022 のランナーでも WebView2 のウィンドウを開けます)。

---

## 注意点

- **git clone を使わない理由**: 所有者を記録しないドライブ (exFAT や一部のネットワークドライブなど) では、FetchContent の `git clone` が "dubious ownership" で失敗します。そのため、依存ライブラリはすべて GitHub のアーカイブで取得しています。リポジトリを Private にすると認証なしではアーカイブを取得できないので、その場合は開発用のプリセットか install 済みのパッケージを使ってください。
- **AVX2**: SunsetMathLib は既定で `/arch:AVX2` を利用側にも付けます (`MATHLIB_ENABLE_AVX2`)。そのため、SunsetEditor は AVX2 に対応した CPU でしか動きません。
- **node_modules**: フロントエンドの依存パッケージは `editor/frontend/node_modules` に入ります (git の管理外)。
