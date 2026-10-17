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
│ │ │ アウトライナー │ │  <div data-viewport>  (空けておく)  │ │ インスペクター (スキーマから生成) │ │ │
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
| `pipeline.list` / `pipeline.get` / `pipeline.update` | パイプラインの記述子の取得と変更 (変更は PSO を作り直す) | M1 の「記述子の JSON の往復」 (M2c で実装) |

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
│   ├── render/SceneRenderer.{h,cpp}← ビューポートの描画 (M2 はサンプルのシーンを移植)
│   └── services/                   ← app / viewport / log / stats / schema / pipeline のハンドラー
└── frontend/                       ← Vite のプロジェクト
    ├── package.json / package-lock.json / vite.config.ts / tsconfig.json / index.html
    └── src/
        ├── main.tsx / App.tsx
        ├── bridge/                 ← client.ts (call / on)、protocol.ts (型)
        ├── panels/                 ← Viewport / Inspector / Log / Stats
        └── inspector/SchemaForm.tsx← スキーマからフォームを生成する
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
| @xyflow/react (React Flow) | レンダーグラフのノードエディター (M4) | MIT | 同上 (M4 で追加) |

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
| **M2d** 統計 | `stats` イベントで、フレーム時間とパスごとの GPU 時間を表示する | 値が 250ms ごとに更新される |

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

---

## リスクと未決事項

- **macOS / Linux の子ウィンドウ**: macOS では WKWebView の上に `NSView` (`CAMetalLayer`) を重ねる。Linux (特に Wayland) では、GTK の上にネイティブの描画面を重ねる方法が限られる。どちらも、Vulkan バックエンドを作り直すときに検証する。M2 は Windows で作る。
- **フォーカスとショートカット**: WebView と子ウィンドウの間で、キーボードのフォーカスとショートカットの扱いを調整する必要がある。
- **DPI**: CSS px と物理ピクセルの変換。モニターをまたいでウィンドウを移動したときの再計算。
- **WebView2 ランタイムがない環境**: `webview_create` が `WEBVIEW_ERROR_MISSING_DEPENDENCY` を返したら、インストールを案内して終了する。
- **Web の成果物の大きさ**: 1 つの HTML にまとめる方式が重くなった場合の、カスタムスキーマへの移行。
- **将来の切り出し候補**: `WebHost` と `Bridge` は、ほかのツールでも使える汎用の仕組みになりそうなので、育ってきたら独立したライブラリ (例: SunsetWebHost) に切り出すことを検討する。
