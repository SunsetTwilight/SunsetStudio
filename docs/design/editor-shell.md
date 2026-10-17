# M2: エディターの骨格 (webview + Web UI) の設計

SunsetEditor の土台になる部分の設計です。UI は Web 技術で作り、3D ビューポートはエンジンが描画します。

- 全体の方針とエンジン側の設計: SunsetGraphicsEngine の [docs/design/editor-architecture.md](https://github.com/SunsetTwilight/SunsetGraphicsEngine/blob/main/docs/design/editor-architecture.md)
- この文書の範囲: M2 (ウィンドウ、UI の読み込み、C++ と JS の橋渡し、ビューポート、インスペクターと統計の最初の版)

---

## 決定事項

| 項目 | 決定 | 理由 |
|------|------|------|
| UI の方式 | 保持モードの Web UI (ImGui は使わない) | 毎フレーム UI を描き直す即時モードを避けたい。UI の表現力 (ノードエディター、レイアウト) |
| WebView | [webview/webview](https://github.com/webview/webview) 0.12.0 (MIT) | OS ごとの WebView をまとめて扱える (Windows: WebView2、macOS: WKWebView、Linux: WebKitGTK)。軽い。C の API で依存が少ない |
| 3D ビューポート | Web のレイアウトで空けた場所に、ネイティブの子ウィンドウを重ねる | 確実で軽い。エンジンのスワップチェーンをそのまま使える |
| フロントエンド | React + TypeScript + Vite | 型があり、部品が豊富。開発中はホットリロードできる |
| C++ と JS のやり取り | JSON の RPC (`studio.call`) とイベント (`studio.on`) | 記述子はスキーマを通して JSON にする設計 (Phase 2〜3) とそのまま合う |

---

## 全体の構成

```
┌──────────────────────────── トップレベルのウィンドウ (webview が作る) ─────────────────────────────┐
│ ┌──────────────── WebView (Web UI 全体) ────────────────────────────────────────────────────────┐ │
│ │  メニュー / ツールバー                                                                          │ │
│ │ ┌───────────────┐ ┌────────────────────────────────────┐ ┌─────────────────────────────────┐ │ │
│ │ │ レンダーグラフ │ │  <div data-viewport>  (空けておく)  │ │ インスペクター (スキーマから生成) │ │ │
│ │ │               │ │ ┌────────────────────────────────┐ │ │                                 │ │ │
│ │ │               │ │ │ ビューポートの子ウィンドウ       │ │ │                                 │ │ │
│ │ │               │ │ │ (エンジンのスワップチェーン)     │ │ │                                 │ │ │
│ │ │               │ │ └────────────────────────────────┘ │ │                                 │ │ │
│ │ └───────────────┘ └────────────────────────────────────┘ └─────────────────────────────────┘ │ │
│ │  ログ / GPU 時間                                                                               │ │
│ └────────────────────────────────────────────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

- トップレベルのウィンドウは webview に作らせ (`webview_create(debug, nullptr)`)、`webview_get_window` でネイティブのハンドルを取得する。
- ビューポートの子ウィンドウは、トップレベルのウィンドウの子として作り、WebView のコントロールより手前 (Z 順で上) に置く。
- WebView は自分で描画するので、その上にネイティブの描画を「重ねて合成」することはできない。子ウィンドウはその矩形を完全に覆う (下記「重ね合わせの制約」)。

---

## スレッドの構成

| スレッド | 持つもの | してよいこと |
|---------|---------|-------------|
| **UI スレッド** (main) | webview、トップレベルと子ウィンドウ、`Bridge` | webview の API、ウィンドウの操作 (移動、表示)、OS のイベント処理 |
| **描画スレッド** | `GraphicsEngine`、シーン、パイプライン | エンジンの API はすべてこのスレッドからだけ呼ぶ |

ルール:

1. **エンジンは描画スレッドだけが触る。** エンジンの API はスレッドセーフではない (同じデバイスを複数のスレッドから呼ばない)。
2. **UI スレッドは描画スレッドを同期的に待たない。** Windows では、スワップチェーンの Present がウィンドウのスレッドにメッセージを送ることがあるため、UI スレッドが描画スレッドを待つとデッドロックし得る。
3. **やり取りはキューで行う。**
   - UI → 描画: コマンドのキュー (`RenderCommand`: ビューポートの矩形の変更、記述子の変更、カメラの入力など)。
   - 描画 → UI: `webview_dispatch` で UI スレッドに処理を渡し、`webview_return` / `webview_eval` で JS に結果やイベントを届ける。
4. 描画スレッドは vsync に合わせてビューポートを描く。ビューポートが非表示 (サイズ 0、最小化) のときは描画を止める。

---

## C++ と JS の橋渡し (Bridge)

### JS → C++: RPC

webview に関数を 1 つだけ登録する (`webview_bind(w, "__studio_call", ...)`)。フロントエンドはこれを包んだ `studio.call(method, params)` を使い、Promise で結果を受け取る。

```ts
// frontend/src/bridge/client.ts (型は protocol.ts にまとめる)
const info = await studio.call("app.info", {});
await studio.call("viewport.setRect", { x, y, width, height, dpr });
```

C++ 側の `Bridge` は、メソッド名からハンドラーを引いて呼ぶだけの小さな仕組みにする。

```cpp
bridge.registerMethod("app.info", [&](const json& params, Reply reply) {
    reply.ok({ {"engineAbi", GFX_ABI_VERSION}, {"adapter", ...} });
});
```

- `Bridge` は webview に依存しない (JSON を受け取って JSON を返すだけ)。そのため、doctest で単体テストできる。
- ハンドラーは UI スレッドで呼ばれる。エンジンの処理が必要なものは描画スレッドのキューに積み、完了時に `reply` を (UI スレッドで) 呼ぶ。
- エラーは `{ "code": ..., "message": ... }` で返し、JS 側では Promise の reject になる。
- メッセージの形式にはバージョンを持たせる (`app.info` の `protocol`)。

### C++ → JS: イベント

`webview_eval` で `window.__studio_event(name, payload)` を呼ぶ。フロントエンドは `studio.on(name, handler)` で受け取る。

| イベント | 内容 | 頻度 |
|---------|------|------|
| `log` | エンジンのデバッグメッセージ (`DeviceConfig::debugMessage`) とアプリのログ | 発生のたび |
| `stats` | フレーム時間、パスごとの GPU 時間 (タイムスタンプのクエリ) | 250ms ごと |
| `viewport.state` | ビューポートの状態 (サイズ、表示 / 非表示) | 変化のたび |

### M2 で用意するメソッド

| メソッド | 内容 | 依存 |
|---------|------|------|
| `app.info` | バージョン、アダプター名、ABI、プロトコルのバージョン | — |
| `viewport.setRect` | ビューポートの矩形 (CSS px) と `devicePixelRatio` | — |
| `viewport.setVisible` | 表示 / 非表示 | — |
| `schema.types` / `schema.describe` | 記述子の型の一覧と、フィールド表 (名前、型、列挙子、範囲、説明) | エンジンのスキーマ (M2c で実装) |
| `pipeline.list` / `pipeline.get` / `pipeline.update` | パイプラインの記述子の取得と変更 (変更は PSO を作り直す) | M1 の「記述子の JSON の往復」 (M2c で実装)。M3d でパイプラインのアセットが対象になった |

---

## ビューポート

### 矩形の同期

1. フロントエンドの `<div data-viewport>` を `ResizeObserver` とウィンドウのリサイズで監視し、`requestAnimationFrame` に 1 回まとめて `viewport.setRect` を呼ぶ (CSS px と `devicePixelRatio`)。
2. UI スレッドは、物理ピクセルに変換して子ウィンドウを移動する (Windows では `SetWindowPos`)。
3. 描画スレッドにサイズの変更を送り、描画スレッドがスワップチェーンをリサイズする (`RenderWindow::resize`)。

- 座標は WebView のクライアント領域が基準。WebView がトップレベルのクライアント領域全体を覆っている前提にする。
- 要素が隠れたとき (タブの切り替え、サイズ 0) は子ウィンドウを非表示にし、描画を止める。

### 入力

- 子ウィンドウはマウスとキーボードの入力を直接受け取る。カメラの操作 (オービット、パン、ズーム) は C++ 側で処理し、描画スレッドに送る。
- フォーカスは、ビューポートをクリックしたら子ウィンドウ、それ以外は WebView に移る。ショートカットキーは両方で同じように効くように、子ウィンドウのキー入力のうちアプリのショートカットに当たるものは JS にイベントで転送する。

### 重ね合わせの制約

WebView の上に、ネイティブの描画を合成することはできない (子ウィンドウが常に手前に出る)。そのため次のようにする。

- メニュー、ドロップダウン、ツールチップなどは、ビューポートに重ならないレイアウトにする。
- モーダルのダイアログなど、どうしても重なる場合は、その間だけ子ウィンドウを隠す。
- 将来、エンジンに「描画結果を CPU に読み戻す API」(リードバック) を追加したら、隠す間に静止画をプレースホルダーとして表示する (エンジンへの要望。テストでのピクセル検証にも使える)。

---

## UI の読み込み

| モード | 読み込み方 | 用途 |
|-------|-----------|------|
| 配布 / 通常の実行 | Vite でビルドした成果物を **1 つの HTML にまとめ** (vite-plugin-singlefile)、`bin/ui/index.html` に置く。起動時に読み込んで `webview_set_html` で表示する | 全 OS で同じ方法で動く。ローカルのファイルを `file://` で開く方法は、モジュールのスクリプトの制限で OS ごとの差が大きいため使わない |
| 開発 | 環境変数 `STUDIO_UI_DEV_URL` (例 `http://localhost:5173`) があれば、`webview_navigate` で Vite の開発サーバーを開く | UI をホットリロードしながら作る |

- 外部のサイトには遷移させない。開発用の URL は `localhost` だけを許可する。
- 1 つの HTML にまとめる方式で、大きさや読み込み時間が問題になった場合は、OS ごとのカスタムスキーマ (WebView2 の仮想ホストのマッピング、WKURLSchemeHandler、WebKitGTK の URI スキーム) に移行する。

---

## リポジトリ内の配置 (SunsetStudio)

```
editor/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                    ← 起動、スレッドの構成
│   ├── app/EditorApp.{h,cpp}       ← 全体の寿命
│   ├── host/WebHost.{h,cpp}        ← webview の包み (UI スレッド)
│   ├── host/Bridge.{h,cpp}         ← RPC の登録と呼び出し (webview に依存しない)
│   ├── viewport/ViewportWindow.h   ← OS ごとの子ウィンドウの抽象
│   ├── viewport/ViewportWindow_win32.cpp
│   ├── render/RenderThread.{h,cpp} ← エンジンを持つスレッド、コマンドのキュー
│   ├── render/ViewportRenderer.{h,cpp} ← ビューポートをレンダーグラフで描く (M3d)
│   ├── host/FileDialog*            ← OS のファイル / フォルダの選択ダイアログ (M4a)
│   └── services/                   ← SchemaJson (スキーマと JSON)、Project (プロジェクトファイルと最近の一覧、M4a)
├── templates/deferred/             ← プロジェクトのテンプレート (中身が新しいプロジェクトの Assets/ になる)
│   ├── graphs/viewport.json        ←   レンダーグラフ (GBuffer → Lighting → Grid → Composite)
│   ├── pipelines/*.json            ←   パイプライン (シェーダー、バインディングレイアウト、ステート)
│   └── shaders/*.hlsl              ←   シェーダー
└── frontend/                       ← Vite のプロジェクト
    ├── package.json / package-lock.json / vite.config.ts / tsconfig.json / index.html
    └── src/
        ├── main.tsx / App.tsx
        ├── bridge/                 ← client.ts (call / on)、protocol.ts (型)
        ├── panels/                 ← Viewport / Graph / Inspector / Log
        ├── state/                  ← store.ts (イベントの結果と選択)、graph.ts (グラフの読み込みと編集)
        └── inspector/              ← SchemaForm.tsx (スキーマからフォーム)、GraphEditors.tsx (パスとリソース)
tests/
└── test_bridge.cpp                 ← Bridge のディスパッチとエラー処理の単体テスト
```

---

## 依存ライブラリ

| ライブラリ | 用途 | ライセンス | 置き場所 |
|-----------|------|-----------|---------|
| webview/webview 0.12.0 | WebView | MIT | `cmake/StudioDependencies.cmake` (アーカイブ + SHA256)。静的ライブラリとしてビルドする |
| WebView2 SDK (ヘッダー) | Windows の WebView | BSD-3 (SDK) | webview の CMake が configure 時に NuGet から取得する |
| nlohmann/json | Bridge の JSON | MIT | 既存 |
| React / React DOM | UI | MIT | `frontend/package.json` |
| TypeScript / Vite / vite-plugin-singlefile | ビルド | Apache-2.0 / MIT / MIT | 同上 (開発時のみ) |
| dockview | ドッキングできるパネルのレイアウト | MIT | 同上 |
| @xyflow/react (React Flow) 12.11.6 | レンダーグラフのノードエディター (M4) | MIT | 同上 |
| @dagrejs/dagre 3.1.1 | ノードの自動配置 (左 → 右に並べる) | MIT | 同上 |
| Vitest 5.0.3 | UI の単体テスト (ノードエディターのモデル) | MIT | 同上 (開発時のみ) |

- ImGui と imnodes は M2a で取り除いた。
- 実行時には、Windows では WebView2 ランタイムが必要 (Windows 11 には標準で入っている)。macOS は標準の WKWebView、Linux は WebKitGTK のパッケージが必要。

### ビルドとの統合

- CMake のオプション `STUDIO_BUILD_UI` (既定 ON) で、`npm ci` と `npm run build` を実行し、`bin/ui/index.html` に配置する。Node.js が見つからなければ configure をエラーにする (メッセージでインストールを案内する)。
- Node.js のバージョンは `frontend/.nvmrc` と `package.json` の `engines` で固定する (LTS)。
- CI では `actions/setup-node` で Node.js を用意する。

---

## 段階と完了条件

| 段階 | 内容 | 完了条件 |
|------|------|---------|
| **M2a** 骨格 | webview のウィンドウ、UI の読み込み (配布 / 開発)、Bridge と `app.info`、`log` イベント、ImGui の削除 | 起動して UI が表示される。`app.info` が往復する。エンジンのメッセージがログのパネルに出る。Bridge の単体テストと UI のビルドが CI で通る |
| **M2b** ビューポート | 子ウィンドウ、描画スレッド、矩形の同期、リサイズ / DPI / 最小化、カメラ操作。サンプルのシーンを描く | パネルのリサイズや分割の変更に子ウィンドウが追従する。D3D12 のメッセージが 0 件。UI がビューポートの描画で固まらない |
| **M2c** インスペクター | `schema.describe` からフォームを自動生成し、`pipeline.update` で PSO を作り直す | UI でカリングなどを変えると、ビューポートにすぐ反映される。不正な値はエラーとしてログに出る |
| ~~**M2d** 統計~~ | `stats` イベントで、フレーム時間とパスごとの GPU 時間を表示する | フレーム時間は M2b で入れた。パスごとの GPU 時間は、エンジンのレンダーグラフの実行 (M3b) で測るので、M3 に含める |
| **M4a** プロジェクトファイル | `.sunsetproj` の読み書き、開く / テンプレートから新規作成 / 最近のプロジェクト、保存先をプロジェクトの `Assets/` にする (下の「プロジェクトファイル」) | `.sunsetproj` を指定して起動すると、そのプロジェクトのグラフで描き、編集の保存がプロジェクトに書かれる。テンプレートから作ったプロジェクトがすぐ描ける |

- M2a と M2b は、M1 (Phase 2c) と並行して進められる。
- M2c は、M1 の「記述子の JSON の往復」に依存する (M1 はエンジン v0.2.0 で完了)。

### M2a の実装状況 (完了)

| 項目 | 内容 |
|------|------|
| 依存 | エンジンを v0.2.0 (ABI 7) に上げた。webview 0.12.0 を静的ライブラリで追加。ImGui / imnodes を削除。プリセットの `GFX_DXC_DIR` を外した (DXC はエンジンが取得する) |
| ウィンドウ / UI | `WebHost`。配布時は `bin/ui/index.html` (1 ファイル、約 230 KB) を `webview_set_html` で読み込む。`STUDIO_UI_DEV_URL` が localhost の URL なら開発サーバーを開く。読み込めなければ案内のページを表示する |
| Bridge | `studio_bridge` (webview に依存しない静的ライブラリ)。`window.__studio_call(method, params)` → `Bridge::handleCall`。返答は UI スレッドに転送してから `webview_return`。エラーコードは JSON-RPC と同じ値。返答しないまま破棄された呼び出しは内部エラーとして返す |
| イベント | `WebHost::emit`。フロントエンドが `ui.ready` を呼ぶまで C++ 側でためる。ページ側も初期化スクリプト (`webview_init`) でためておき、`installEventSink` で受け取り口を差し替える |
| 描画スレッド | `RenderThread`。エンジンの初期化 / 終了と、頼まれた処理の実行だけ (フレームのループは M2b)。エンジンのデバッグメッセージは `log` イベントになる (id から source を shader / pipeline / engine / d3d12 に分ける) |
| メソッド / イベント | `ui.ready`、`app.info`、`app.ping` / `log`、`engine.status` (型は `frontend/src/bridge/protocol.ts`) |
| フロントエンド | React 19 + TypeScript 7 + Vite 8 + vite-plugin-singlefile。ログのパネル (重大度の絞り込み、検索、追従スクロール)、ステータスバー (エンジンの状態)。ビューポートとインスペクターは場所だけ |
| ビルド | `cmake/StudioFrontend.cmake`: `package-lock.json` が変わったら `npm ci`、ソースが変わったら `npm run build`。Node.js がなければ configure をエラーにする (`STUDIO_BUILD_UI=OFF` で回避) |
| テスト | Bridge の単体テスト (`unit`)、`SunsetEditor --smoke-test --software` (`editor_smoke`、ラベル `gui`)。CI はスモークテストを `continue-on-error` で実行する |

M2b に持ち越し: dockview によるドッキング (今は CSS Grid の固定レイアウト)、Release ビルドのコンソールを出さない設定。

### M2b の実装状況 (完了)

| 項目 | 内容 |
|------|------|
| 子ウィンドウ | `ViewportWindow` (`viewport/ViewportWindow_win32.cpp`)。トップレベルの子 (`WS_CHILD | WS_CLIPSIBLINGS`) として作り、`setRect` のたびに `HWND_TOP` で WebView より手前に置く。背景は消去しない (エンジンが描く) |
| 矩形の同期 | フロントエンドの `ViewportPanel` が `requestAnimationFrame` ごとに `getBoundingClientRect` を調べ、変わったときだけ `viewport.setRect` (CSS px と `devicePixelRatio`) を送る。パネルの移動 / 分割 / ウィンドウのリサイズのどれでも追従する。パネルのドラッグ中 (dockview のドロップ先の表示が重なる) は隠す |
| 描画スレッド | `RenderThread` のフレームのループ。ビューポートが見えていて大きさがあり、トップレベルが最小化されていないあいだだけ描く (vsync)。それ以外は処理の依頼を待つ (100 ms ごとに起きて状態を確認)。スワップチェーンは最初に見えたときに作り、大きさが変わったら `resize` する。バッファは 3 枚 |
| シーン | `SceneRenderer`: 立方体 49 個と地面のグリッド。1 パスのフォワード (ルート CBV b0 = フレームの定数、ルート定数 b1 = オブジェクトの行列と色)。シェーダーは `editor/shaders/*.hlsl` を `ShaderLibrary` で読み、ビルドツリーではソースのフォルダを直接読むので、編集するとその場で反映される。ディファードの合成はレンダーグラフ (M3) の上で作り直す (計画の「サンプルのシーンを移植」から変更) |
| カメラ | `OrbitCamera`: 左ドラッグ = 回転、中 / 右ドラッグ = 平行移動 (注視点の距離に比例)、ホイール = ズーム、F = 視点を戻す。入力は子ウィンドウ (UI スレッド) でためて、描画スレッドが毎フレーム取り出す。行列は自前 (SunsetMathLib の `perspective` は深度 -1〜1 の OpenGL 式のため) |
| 閉じる処理 | ×ボタン (`WM_CLOSE`) をトップレベルのサブクラスで受け、描画スレッドにスワップチェーンを解放させてから閉じる。`RenderThread::stop` はメッセージを処理しながら待つ (スワップチェーンの破棄がウィンドウのスレッドにメッセージを送ってもデッドロックしない) |
| 統計 | `stats` イベント (約 0.5 秒ごと): fps、フレーム時間、CPU 時間 (記録と submit)、大きさ。ステータスバーに表示する (GPU のパスごとの時間は M2d) |
| ドッキング | dockview-react 8.6 (MIT)。アウトライナー / ビューポート / インスペクター / ログ。状態は小さなストア (`state/store.ts`) を `useSyncExternalStore` で読む |
| エンジンの修正 | 終了時のスワップチェーンの破棄で、D3D12 のデバッグレイヤーが CORRUPTION を報告した。原因はエンジンのスワップチェーンのフェンス値が単調に増えていなかったこと (バッファ 3 枚で表に出た)。エンジン v0.2.1 で修正 |
| テスト | スモークテストは、ビューポートが 30 フレーム描けて、D3D12 のエラー / 警告が 0 件であることまで確認する (終了コード 5 = D3D12 のメッセージあり)。手元では、開いてすぐ×で閉じる操作をタイミングを変えて 8 回、ドラッグ / ホイール / リサイズ / 最小化と復帰を確認した |

M2c 以降に持ち越し: ビューポートにフォーカスがあるときのショートカットキーの JS への転送、レイアウトの保存、Release ビルドのコンソールを出さない設定。

### M2c の実装状況 (完了)

| 項目 | 内容 |
|------|------|
| メソッド | `schema.types` (型の一覧)、`schema.describe {type}` (型と、その中で使う構造体 / 列挙型の説明をまとめて返す)、`pipeline.list` / `pipeline.get {id}` / `pipeline.update {id, desc}` |
| JSON の橋渡し | `studio_services` (`services/SchemaJson.*`)。エンジンの `GfxJson.h` と Bridge の `nlohmann::json` を文字列で変換する。`pipeline.get` は既定値も含めた全フィールドを返す (ハンドルと `const char*` は出さない)。`pipeline.update` は今の記述子に、書かれたフィールドだけを上書きする (ハンドルはそのまま)。GPU を使わずに単体テストする |
| 作り直し | パイプラインは描画スレッドが持つので、`RenderThread::postScene` で描画スレッドに頼み、`engine.updatePipeline` で PSO を作り直してから返答する。値が不正なら `InvalidParams` (フィールドのパス付き)、PSO を作れなければ `Internal` で、どちらもエンジン側は元のまま。理由はログのパネルにも出る |
| フォーム | `inspector/SchemaForm.tsx`: フィールド表からフォームを作る (bool / enum / flags / 数値 / 文字列 / 構造体 / 固定長配列)。数値は Enter かフォーカスを外したときに確定する。既定値と違うフィールドは名前を強調し、tooltip に対応する D3D12 のメンバーを出す。絞り込みの欄に入力すると、一致するフィールドをパス付きで並べる |
| インスペクター | `panels/InspectorPanel.tsx`: パイプラインの選択、変更ごとに `pipeline.update` (送信中の変更は最後の値だけを送る)、結果 (反映 / 警告 / エラー) の表示、「元に戻す」(エンジンの今の値を読み直す) |
| 対象 | ビューポートの 2 つのパイプライン (立方体、グリッド)。レンダーグラフ (M3) ができたら、グラフのパスのパイプラインに広げる |
| テスト | `studio_tests_unit` に `test_schema_json.cpp` (型の説明、往復、部分的な上書き、不正な値、推奨範囲の警告)。スモークテストは、ビューポートの描画のあと、`pipeline.update` を Bridge の同じ経路 (`Bridge::callLocal`) で呼び、正しい変更が反映されること、不正な値がエラーになること、元に戻したあとも描けて D3D12 のメッセージが 0 件であることを確認する (終了コード 6 = インスペクターの経路の失敗)。手元では、実際のマウスとキー入力で fillMode を Wireframe にしてビューポートに反映されることを確認した |
| CI | スモークテストを必須にした (M2a / M2b で GitHub のランナーでも安定して通ったため)。actions を v5 に上げた |

### M3d の実装状況 (完了)

エンジンの M3 (レンダーグラフ) の最後の段階。ビューポートをグラフで描き、パスの一覧と GPU 時間を表示し、インスペクターでパス / リソース / パイプラインを編集する。

| 項目 | 内容 |
|------|------|
| アセット | `editor/assets/` (`graphs/viewport.json`、`pipelines/*.json`、`shaders/*.hlsl`)。ビルドツリーの exe はソースのフォルダを直接読み書きする (保存するとその場で反映され、エディターで保存した内容はソースに残る)。配布用は `bin/assets/` にコピーする |
| ビューポート | `ViewportRenderer` (`SceneRenderer` を置き換え): `PipelineLibrary`、`GraphAsset`、`Executor` を持ち、`IDrawListProvider` としてシーン (立方体 49 個は `Opaque`、地面のグリッドは `Grid` のレイヤー) とフレームの定数 (`frame`) を渡す。オブジェクトの定数は、オブジェクトごとの定数バッファを `DrawContext::paramOf("object")` につなぐ。グラフは GBuffer → Lighting (半球のアンビエント + ランバート + ブリン・フォン) → Grid (深度を DSVRead で読んで重ねる) → Composite (ガンマ) |
| 変更の反映 | 描画スレッドが毎フレーム、シェーダー → パイプライン → グラフの順に変更を調べて反映し、結果をログに出す。グラフが切り替わると `graph.changed` を送り、UI が読み直す |
| メソッド | `graph.get` (グラフの JSON、実行順、実行しないパスと理由、診断、リソースの寿命、パイプラインのアセットの一覧)、`graph.set {graph}` (グラフ全体の JSON に切り替える。読めない / コンパイルできなければ `InvalidParams` で前のまま)、`graph.save`。`pipeline.*` はパイプラインのアセット (id はパス) を対象にし、`pipeline.save` を加えた |
| イベント | `stats` にパスごとの GPU 時間 (`passes`) とグラフ全体 (`gpuMs`) を加えた。`graph.changed {revision}` |
| UI | レンダーグラフのパネル (`panels/GraphPanel.tsx`。アウトライナーを置き換え): パスを宣言順に並べ、実行順の番号、GPU 時間の棒、実行しない理由 (無効 / 条件 / 結果に届かない) を出す。チェックボックスで有効 / 無効、クリックでインスペクターに表示、保存。リソースの一覧、コンパイルの診断 |
| インスペクター | 選んだものに合わせて切り替える。パス: 有効 / 無効、パイプライン (アセットの一覧から)、order、入力と出力 (リソース、使い方、load、bind の param / offset、追加と削除)。リソース: フォーマット (スキーマの GfxFormat)、大きさ、mips、samples、寿命。パイプライン: M2c のフォーム (アセットの state を編集)、保存 |
| エンジンの修正 | 深度を DSV / DSVRead だけで使うグラフ (Lighting を無効にしたとき) で、テクスチャが DENY_SHADER_RESOURCE で作られ、DSVRead の遷移が D3D12 のエラーになっていた。Executor が一時テクスチャを常に SRV 付きで作るように直した (エンジン v0.3.0) |
| テスト | スモークテストに、グラフの経路を加えた: `graph.set` で Grid を無効にする (実行するパスから外れる) → Lighting を無効にする (深度を SRV で読まない形。上の不具合を再現する) → コンパイルできないグラフ (Composite のパイプラインが空) がエラーになる → 元に戻す。そのあとも描けて D3D12 のメッセージが 0 件であることを確認する (終了コード 7 = グラフの経路の失敗)。手元では、WebView2 のリモートデバッグ (CDP) で DOM を操作し、パスの無効化、パスとリソースの表示、フォーマットの変更、コンパイルできない変更 (エラーで前のまま)、パイプラインの編集への切り替えを確認した |

---

## プロジェクトファイル (M4a、2026-10-17 決定)

作品ごとのフォルダを、プロジェクトファイル `.sunsetproj` で表す (Unreal Engine の `.uproject`、Unity のプロジェクトのフォルダ、
Visual Studio の `.sln` に当たる)。今のエディターは自分のソース (`editor/assets/`) を直接読み書きしているので、M4 の最初に入れて解消する。

### フォルダの構成

```
MyGame/
├── MyGame.sunsetproj      ← プロジェクトファイル (JSON。git で管理する)
├── Assets/                ← 作品のアセット (git で管理する)
│   ├── graphs/            ←   レンダーグラフ
│   ├── pipelines/         ←   パイプライン
│   ├── shaders/           ←   シェーダー
│   └── (models/ textures/ は SunsetAssets ができてから)
└── Saved/                 ← エディターが作るもの (git で管理しない)
    ├── Layout.json        ←   パネルの配置、カメラの位置
    └── Cache/             ←   シェーダーや PSO のキャッシュ (将来)
```

### プロジェクトファイルの中身

エンジンのアセットと同じく JSON。知らないキーは警告にして無視する (新しいファイルを古いエディターで開けるように)。

```json
{
  "version": 1,
  "name": "MyGame",
  "engine": "0.3",
  "assets": "Assets",
  "viewport": { "graph": "graphs/viewport.json" }
}
```

| キー | 内容 |
|------|------|
| `version` | ファイルの形式のバージョン |
| `name` | 表示名 (ウィンドウのタイトル、最近のプロジェクトの一覧) |
| `engine` | 作ったときのエンジンのバージョン。今のエンジンより新しければ警告する |
| `assets` | アセットのフォルダ (プロジェクトファイルからの相対パス)。`PipelineLibrary::setRoot` とグラフのパスの基準になる |
| `viewport.graph` | ビューポートを描くグラフ (`assets` からの相対パス) |

### エディターでできるようにすること

| 機能 | 内容 |
|------|------|
| 開く | コマンドラインで `.sunsetproj` を渡す (`SunsetEditor.exe MyGame.sunsetproj`)。起動画面に「最近のプロジェクト」の一覧を出す (ユーザーごとの設定に保存する)。ファイルの関連付けで、ダブルクリックでも開けるようにする |
| 新規作成 | テンプレートから作る。今の `editor/assets` を「ディファード」のテンプレートにし、`Assets/` にコピーしてプロジェクトファイルを書く |
| 切り替え | 開いているプロジェクトを閉じて別のものを開く (描画スレッドで PipelineLibrary / Executor / グラフを作り直す) |
| 保存先 | グラフとパイプラインの保存は、開いているプロジェクトの `Assets/` に書く。パネルの配置とカメラは `Saved/Layout.json` |
| メソッド | `project.info` / `project.open {path}` / `project.create {path, template}` / `project.recent`。イベント `project.changed` |

- 置き場所は SunsetStudio。エンジンは「基準のフォルダ」を受け取るだけなので、変更は要らない。
- スモークテストは、一時フォルダにテンプレートからプロジェクトを作って開く (ソースのフォルダを書き換えない)。
- プロジェクトを指定せずに起動したときは、起動画面 (最近のプロジェクト / 新規作成 / 開く) を出す (2026-10-17 決定)。

### M4a の実装状況 (完了)

| 項目 | 内容 |
|------|------|
| プロジェクトファイル | `services/Project.*` (GPU / webview に依存しない): `.sunsetproj` の読み書き (型の違いはエラー、知らないキーは警告、`assets` は相対パスだけ、プロジェクトの `engine` が新しければ警告)、テンプレートからの作成 (`<場所>/<名前>/<名前>.sunsetproj`、`Assets/` にテンプレートをコピー、`Saved/` と `.gitignore`。名前の検査、空でない既存のフォルダには作らない)、最近のプロジェクト (`%LOCALAPPDATA%/SunsetStudio/recent.json`、新しい順に最大 12 件、なくなったものは印を付ける) |
| テンプレート | エディターのアセットを `editor/templates/deferred/` に移した (ビルドツリーではソース、配布では `bin/templates/`)。エディターがソースのアセットを書き換えることはなくなった |
| 描画スレッド | `RenderThread::openProject` / `closeProject`。プロジェクトを開くたびに `ShaderLibrary` と `ViewportRenderer` を作り直す (前のプロジェクトのシェーダーの検索パスを持ち越さない)。開いていないあいだは描かない |
| メソッド | `project.info` / `project.recent` (一覧と、新規作成の既定の場所 `ドキュメント/SunsetStudio`) / `project.templates` / `project.open {file}` / `project.create {location, name, template}` / `project.close` / `project.layout.get` / `project.layout.set {layout}` / `dialog.openProject` / `dialog.pickFolder {initial}` (`IFileOpenDialog`)。イベント `project.changed` |
| 起動 | コマンドラインに `.sunsetproj` があれば開く。なければ起動画面。ウィンドウのタイトルは「<名前> - SunsetEditor」 |
| UI | `panels/StartScreen.tsx`: 最近のプロジェクト (クリックで開く)、「開く…」、新規作成 (名前、場所と「参照…」、テンプレート、作るファイルのパスの表示)。ツールバーにプロジェクト名と「閉じる」。プロジェクトが変わったら、グラフ / 選択 / 統計を捨てて読み直す |
| パネルの配置 | dockview の配置を `Saved/Layout.json` に保存し (変わってから 0.5 秒後)、開いたときに戻す。読めなければ既定の配置。カメラの位置の保存は後で |
| テスト | `studio_tests_unit` に `test_project.cpp` (JSON と診断、テンプレートから作って開く、名前の検査、壊れた / ないファイル、最近の一覧の並びと上限と保存)。スモークテストは一時フォルダにテンプレートから作って開き (最近の一覧には入れない)、最後に `graph.save` がプロジェクトの `Assets/` に書くことを確認する (終了コード 8 = プロジェクトを作って開けない)。終了時に一時フォルダを消す。手元では CDP で、起動画面 → 新規作成 → 閉じる → 最近の一覧から開き直す → 同じ名前での作成がエラー、コマンドラインで開く (タイトル)、サッシのドラッグで配置が保存され次の起動で戻ることを確認した (D3D12 のメッセージ 0 件) |

### M4 Undo / Redo の実装状況 (完了)

| 項目 | 内容 |
|------|------|
| 履歴 | `services/UndoStack.*` (GPU / webview に依存しない): 編集を「種類 (graph / pipeline)、対象、変更前、変更後」の JSON で持つ。新しい編集でやり直しの履歴は消える。変化のない編集は入れない。最大 200 件 |
| 記録 | `graph.set` (グラフ全体の JSON) と `pipeline.update` (アセットの state の全フィールド) が、適用に成功したら変更前と変更後を記録する (`label` で履歴の名前を渡せる)。適用は描画スレッドで行い、履歴は UI スレッドで持つ |
| 元に戻す / やり直す | `edit.undo` / `edit.redo`: 変更前 / 変更後を、編集と同じ経路で適用する。適用できなければ (ファイルの外部での変更で成り立たないなど) エラーで、履歴はそのまま。`edit.state`、イベント `edit.changed`、パイプラインが変わったら `pipeline.changed {id}` |
| 範囲 | ファイルの外部での変更 (ホットリロード) と保存は履歴に入れない。プロジェクトを開く / 閉じると履歴を消す |
| UI | ツールバーの「↶ 元に戻す」「↷ やり直す」(tooltip に次の編集の名前)、Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z (文字を入力している欄ではブラウザの取り消しを優先し、履歴がなければ何もしない)。履歴の名前は、グラフのパネル / パス / リソースの操作と、インスペクターの最後に変えたフィールド ("pipelines/gbuffer.json: rasterizerState.cullMode = None")。インスペクターの「元に戻す」(エンジンの今の値を読み直す) は「読み直す」に名前を変えた |
| テスト | `studio_tests_unit` に `test_undo.cpp` (元に戻す / やり直す、新しい編集でやり直しが消える、変化のない編集、上限)。スモークテストは、グラフの編集のあとに `edit.undo` で前のグラフ (Lighting が無効) に戻り、`edit.redo` で元のグラフになることを確認する。手元では CDP で、Grid の無効化を Ctrl+Z / Ctrl+Y で戻してやり直す、履歴がないときの Ctrl+Shift+Z が何もしない、インスペクターの cullMode の変更をボタンで戻してやり直す (インスペクターも読み直す) ことを確認した (D3D12 のメッセージ 0 件) |
| 修正 | プロジェクトファイルに書くエンジンのバージョンを、`STUDIO_GFX_TAG` (キャッシュに古い値が残る) ではなく、実際に使うエンジン (パッケージのバージョン / ソースの `project(VERSION)`) から取るようにした |

### M4 リソースビューアーの実装状況 (完了)

| 項目 | 内容 |
|------|------|
| 表示 | `render/ResourceViewer.*`: グラフの記録のあとに、同じコマンドバッファでバックバッファに選んだテクスチャを上書きする (縦横比を保って収め、外は市松模様)。チャンネル (1 つだけなら白黒)、mip、深度の線形化 (視点からの距離)、露出 (EV)、表示する範囲、ガンマ。テクスチャは `Executor::recordedTexture` で引き、`AllShaderResource` に遷移して読む (次のフレームは Executor が作成時の状態に戻す) |
| ピクセルの値 | ビューポートのカーソル (`ViewportWindow::cursor`) の下のテクセルを Compute で読んで Storage のバッファに書き、Readback のバッファ 3 つの輪にコピーし、`readBuffer` で読めたものから最新を返す (数フレーム遅れる)。深度は視点からの距離も添える。約 10 回 / 秒、イベント `viewer.pixel` で UI に送る。`pin` を指定すると、カーソルの代わりにそのテクセルを読む (自動テスト用) |
| 対象 | 2D の (配列 / キューブ / 3D でない)、MSAA でない、整数でないテクスチャ。それ以外は理由を `viewer.pixel` の `status` で返し、最終画像のまま |
| メソッド | `viewer.get` / `viewer.set` (書かれた項目だけ変える) / `viewer.pixel.get` |
| UI | `panels/ViewerPanel.tsx` (ログと同じ場所のタブ。ツールバーの「リソースビューアー」で前に出す / 閉じていたら開く): 「ビューポートに表示」、リソース (グラフのテクスチャ。選ぶと種類に合わせた既定の表示)、R / G / B / A、mip、距離にする (深度)、ガンマ、露出、範囲、カーソルの下の値と色見本。インスペクターのリソースに「ビューアーで表示」 |
| テスト | スモークテストは、グラフの確認のあとに gbAlbedo を表示して画面の中央のテクセルを読み、中央の立方体の色 (0.92, 0.55, 0.30) が届くことを確認する (終了コード 9)。そのあと深度を距離で表示してから閉じ、D3D12 のメッセージが 0 件であることを確認する。手元では CDP とウィンドウのスクリーンショットで、法線 / 深度 (距離) / sceneColor の G だけの表示と値、オフで最終画像に戻ることを確認した |
| 必要なエンジン | v0.4.0 (ABI 8 のリードバックと `recordedTexture`) |

### M4 ノードエディターの実装状況 (完了)

| 項目 | 内容 |
|------|------|
| モデル | `nodegraph/model.ts`: グラフの JSON ⇔ ノードと線。ノードはパス (`pass:<名前>`。左に入力 `in:<番号>`、右に出力 `out:<番号>`、新しい入力 / 出力の `in:new` / `out:new`) とリソース (`res:<名前>`。左に書くパス、右から読むパス。`$` で始まるインポートも)。線は 書く (`w:<パス>:<番号>`) と 読む (`r:<パス>:<番号>`)。編集の関数 (つなぐ、線 / ノードを消す、パス / リソースを加える、名前を変える) はすべて JSON を受け取って新しい JSON を返す |
| つなぐ | リソース → パスの入力 (新しい入力は SRV。bind は今ある bind で一番多い param の次の offset。Copy のパスは CopySrc)、パスの出力 → リソース (深度は DSV、色は空いている RTV、Compute は UAV、Copy は CopyDst)。既存のハンドルにつなぐと、そのリソースを差し替える。パスどうし / リソースどうしはつなげない |
| 消す / 加える / 名前 | Delete / Backspace で線 (入力 / 出力から外す) とノード (パスは after から、リソースは参照からも消す)。右クリックのメニューとヘッダーのボタンでパス (Raster / Compute / Copy) とリソース (テクスチャ / バッファ) を、重ならない名前で加える。インスペクターで名前を変える (参照、after、間接描画の引数、位置も直す) |
| 位置 | ノードの位置は、グラフのアセットの `editor.nodes` に `{ "pass:GBuffer": [x, y] }` の形で保存する (エンジン v0.4.1 が `editor` を読み書きで保つ)。位置のないノードは dagre で並べる。「自動配置」で全体を並べ直す。移動も元に戻す / やり直すの対象 |
| 表示 | パス: 種類、名前、実行順、GPU 時間、パイプライン、実行しない理由 (薄く)、入力 / 出力 (リソース、使い方、bind)。リソース: フォーマット、大きさ (インポートは破線、バッファは別の色)。コンパイルのエラー / 警告のあるノードは枠を赤 / 黄にし、tooltip に理由を出す。線は 書く = 橙、読む = 灰で、ラベルは使い方。クリックでインスペクターに表示する |
| 経路 | 編集はすべてグラフ全体の JSON を `graph.set` で送る。コンパイルできない変更 (読みながら書く、bind がテーブルの範囲外など) はエラーで前のまま、状態の欄に理由とパスを出す |
| 配置 | 既定の配置では、ビューポートの下にノードエディター (ログ、リソースビューアーと同じ場所のタブ)。ツールバーの「ノードエディター」で前に出す / 閉じていたら開く |
| テスト | `nodegraph/model.test.ts` (Vitest、12 件。別ウィンドウの実装で 13 件): ノードと線、自動配置、位置の保存、つなぐ (入力 / 出力 / 差し替え / 不正)、線とノードの削除、追加、名前の変更。CTest の `ui_unit` で実行する。手元では CDP のマウスとキーの入力で、ドラッグでの移動と保存と Ctrl+Z、右クリックでの追加、出力 / 入力のハンドルどうしの接続、コンパイルできない接続のエラー、名前の変更、線とノードの削除、自動配置を確認した (D3D12 のメッセージ 0 件) |

### M4 ノードの結果の表示と別ウィンドウの実装状況 (完了)

| 項目 | 内容 |
|------|------|
| ノードの結果 | ノードエディターでノードを右クリックすると、そのノードの結果として表示できるテクスチャ (`previewTargets`: リソースならそれ自身、パスなら出力 → 入力の順。インポートとバッファは除く) を並べる。「ビューポート」はリソースビューアーで最終出力の代わりに表示し (ツールバーに「表示中: … ✕」)、「タブ」はプレビューのパネルを開き、「別ウィンドウ」はプレビューのウィンドウを開く |
| タブのプレビュー | dockview のパネル (`preview:<リソース>`、`PreviewPanel`)。ビューポートと同じ方式で、C++ がメインのウィンドウの子ウィンドウ (`preview.open` の `mode: "panel"`、`FloatingWindow::createChild`) を作り、パネルの矩形に合わせる (`preview.setRect`)。最初のタブはビューポートの右に、2 つ目からは同じグループに並べる。パネルを閉じると `preview.close`。タブをウィンドウの外へドラッグする (またはパネルの「別ウィンドウ」) と、ネイティブの別ウィンドウで開き直してタブを閉じる。配置は Layout.json に残り、プロジェクトを開くと開き直す。リソースの大きさはビューポートの大きさに従う (グラフの `relative`) |
| プレビュー | ネイティブのトップレベルのウィンドウ (`FloatingWindow`。メインのウィンドウを owner にする) に、それぞれスワップチェーンを作る。グラフは 1 フレームに 1 回だけ記録し、そのあとでウィンドウごとに `ResourceViewer::draw` (表示だけ。設定と SRV はウィンドウごと) で描いて Present する (vsync はビューポートだけ)。同じリソースを開き直すと前に出す。表示できない理由はタイトルに出す。メソッド `preview.open / set / close / list`、イベント `preview.changed` |
| ビューポートの切り離し | ビューポートの子ウィンドウを、別ウィンドウ (`FloatingWindow`) へ `SetParent` で移す。移す前にビューポートを隠して、スワップチェーンを解放する (`RenderThread::resetViewport`)。別ウィンドウのあいだ、Web のパネルには「ここに戻す」を出し、C++ は Web の矩形を覚えるだけにする (戻すときに使う)。別ウィンドウを閉じると戻る。メソッド `viewport.float / state`、イベント `viewport.changed` |
| 見えないとき | ビューポートが見えない (タブの裏、最小化) がプレビューはあるときは、グラフをオフスクリーンの `$backbuffer` (最後のビューポートの大きさ) に描く |
| Web のパネル | タブ (またはグループ) をドラッグして、ウィンドウの外で離すと、dockview の popout (WebView2 の `window.open`) で別ウィンドウにする (`state/dock.ts`)。配布時の UI は `about:blank` (`webview_set_html`) なので、同じ `about:blank` を開く (開発サーバーでは `public/popout.html`)。`about:blank` は `window.open` の中で読み込みが終わり `load` が来ないことがあるので、来ていなければ送る。ビューポートのタブを外へ出したときは、ネイティブの切り離しにする |
| 終了 | 別ウィンドウはメインのウィンドウと一緒に壊れるので、閉じる前にプレビューのスワップチェーンとビューポートの別ウィンドウを片付ける。プロジェクトを閉じる / 開くときは、プレビューを閉じ、ビューポートを戻す |
| テスト | スモークテストに、gbAlbedo のプレビュー (別ウィンドウ) と depth のプレビュー (タブ。決まった矩形) を開いて表示できたこと、ビューポートを別ウィンドウにして描けること、戻せることを加えた (終了コード 10)。手元では CDP で、パスの右クリックのメニュー、別ウィンドウ / ビューポートへの表示、同じリソースの開き直し、ログのタブを外へドラッグしての popout (中身が別ウィンドウに移る)、ビューポートの切り離しと戻し、タブのプレビュー (2 つを同じグループに並べる、前に出し直す、外へドラッグして別ウィンドウ、閉じる) を確認した (D3D12 のメッセージ 0 件) |
| 未対応 | popout の窓には WebView2 のアドレス (about:blank) の帯が出る (`NewWindowRequested` で自前の窓にすれば消せる)。プレビューの窓では、まだチャンネルなどを変えられない (`preview.set` はある)。プレビューで最終出力 (`$backbuffer`) は表示できない |

---

## リスクと未決事項

- **macOS / Linux の子ウィンドウ**: macOS では WKWebView の上に `NSView` (`CAMetalLayer`) を重ねる。Linux (特に Wayland) では、GTK の上にネイティブの描画面を重ねる方法が限られる。どちらも、Vulkan バックエンドを作り直すときに検証する。M2 は Windows で作る。
- **フォーカスとショートカット**: WebView と子ウィンドウの間で、キーボードのフォーカスとショートカットの扱いを調整する必要がある。
- **DPI**: CSS px と物理ピクセルの変換。モニターをまたいでウィンドウを移動したときの再計算。
- **WebView2 ランタイムがない環境**: `webview_create` が `WEBVIEW_ERROR_MISSING_DEPENDENCY` を返したら、インストールを案内して終了する。
- **Web の成果物の大きさ**: 1 つの HTML にまとめる方式が重くなった場合の、カスタムスキーマへの移行。
- **将来の切り出し候補**: `WebHost` と `Bridge` は、ほかのツールでも使える汎用の仕組みになりそうなので、育ってきたら独立したライブラリ (例: SunsetWebHost) に切り出すことを検討する。
