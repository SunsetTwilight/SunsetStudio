// ============================================================
//  SunsetEditor の画面
//
//    ┌ ツールバー ───────────────────────────────────────┐
//    │ dockview: レンダーグラフ │ ビューポート │ インスペクター  │
//    │           ログ (ビューポートの下)                       │
//    └ ステータスバー (エンジンの状態、フレームの統計) ──────┘
//
//  パネルはドラッグで移動 / 分割 / タブにできる (dockview)。配置はプロジェクトの Saved/Layout.json に残す。
//  タブをウィンドウの外へドラッグして離すと、そのパネルを別ウィンドウにする (state/dock.ts)。
//  ビューポートの上には C++ の子ウィンドウが重なる (ViewportPanel)。
//  プロジェクトを開いていないときは、起動画面 (StartScreen) を出す (M4a)。
// ============================================================
import { useCallback, useEffect, useState } from 'react';
import { DockviewReact, themeDark, type DockviewReadyEvent, type IDockviewPanelProps, type SerializedDockview } from 'dockview-react';
import 'dockview-react/dist/styles/dockview.css';
import { call, installEventSink, isConnected, on, StudioError } from './bridge/client';
import { PROTOCOL_VERSION, type EngineInfo, type JsonValue } from './bridge/protocol';
import { GraphPanel } from './panels/GraphPanel';
import { InspectorPanel } from './panels/InspectorPanel';
import { LogPanel } from './panels/LogPanel';
import { StartScreen } from './panels/StartScreen';
import { PreviewPanel, type PreviewParams } from './panels/PreviewPanel';
import { AssetPanel, type AssetParams } from './panels/AssetPanel';
import { PipelinePanel, type PipelineParams } from './panels/PipelinePanel';
import { ScenePanel } from './panels/ScenePanel';
import { loadScene } from './state/scene';
import { loadPipelineShaders, loadShaderErrors } from './state/assets';
import { setViewer, ViewerPanel } from './panels/ViewerPanel';
import { NodeEditorPanel } from './nodegraph/NodeEditorPanel';
import { ViewportPanel } from './panels/ViewportPanel';
import { popoutPanel, setDockApi, showPanel, watchDragOut } from './state/dock';
import { errorText, loadGraph } from './state/graph';
import { studioStore, useStudio } from './state/store';

const components: Record<string, React.FunctionComponent<IDockviewPanelProps>> = {
  viewport: () => <ViewportPanel />,
  graph: () => <GraphPanel />,
  inspector: () => <InspectorPanel />,
  log: () => <LogPanel />,
  viewer: () => <ViewerPanel />,
  nodes: () => <NodeEditorPanel />,
  scene: () => <ScenePanel />,
  preview: (props) => <PreviewPanel {...(props as IDockviewPanelProps<PreviewParams>)} />,
  asset: (props) => <AssetPanel {...(props as IDockviewPanelProps<AssetParams>)} />,
  pipeline: (props) => <PipelinePanel {...(props as IDockviewPanelProps<PipelineParams>)} />,
};

// 既定のレイアウト
function defaultLayout(event: DockviewReadyEvent) {
  const api = event.api;
  api.addPanel({ id: 'viewport', component: 'viewport', title: 'ビューポート' });
  api.addPanel({
    id: 'graph',
    component: 'graph',
    title: 'レンダーグラフ',
    position: { referencePanel: 'viewport', direction: 'left' },
    initialWidth: 300,
  });
  api.addPanel({
    id: 'scene',
    component: 'scene',
    title: 'シーン',
    position: { referencePanel: 'graph', direction: 'within' },
  });
  api.addPanel({
    id: 'inspector',
    component: 'inspector',
    title: 'インスペクター',
    position: { referencePanel: 'viewport', direction: 'right' },
    initialWidth: 380,
  });
  api.addPanel({
    id: 'nodes',
    component: 'nodes',
    title: 'ノードエディター',
    position: { referencePanel: 'viewport', direction: 'below' },
    initialHeight: 380,
  });
  api.addPanel({
    id: 'log',
    component: 'log',
    title: 'ログ',
    position: { referencePanel: 'nodes', direction: 'within' },
  });
  api.addPanel({
    id: 'viewer',
    component: 'viewer',
    title: 'リソースビューアー',
    position: { referencePanel: 'nodes', direction: 'within' },
  });
  api.getPanel('nodes')?.api.setActive();
  api.getPanel('viewport')?.api.setActive();
}

export function App() {
  const connError = useStudio((s) => s.connError);
  const [ping, setPing] = useState<string>('');

  useEffect(() => {
    const offLog = on('log', studioStore.addLog);
    const offEngine = on('engine.status', studioStore.setEngine);
    const offStats = on('stats', studioStore.setStats);
    // グラフが切り替わったら (ファイルの保存、インスペクターの編集) 読み直す
    const offGraph = on('graph.changed', (e) => {
      if (studioStore.get().graph?.revision !== e.revision) void loadGraph().catch(() => {});
    });
    const offProject = on('project.changed', studioStore.setProject);
    const offEdit = on('edit.changed', studioStore.setEdit);
    const offViewer = on('viewer.pixel', studioStore.setViewerPixel);
    const offPreview = on('preview.changed', (e) => studioStore.setPreviews(e.previews));
    const offFloat = on('viewport.changed', (e) => studioStore.setViewportFloating(e.floating));
    const offShader = on('shader.status', (e) => studioStore.setShaderStatus(e.path, e.ok, e.text));
    // シーン: 元に戻す / やり直す / 保存で変わったら読み直す。ビューポートのクリックで選ぶ
    const offScene = on('scene.changed', (e) => {
      const cur = studioStore.get().scene;
      if (!cur || cur.revision !== e.revision) void loadScene();
      else if (cur.dirty !== e.dirty) studioStore.setScene({ ...cur, dirty: e.dirty });
    });
    const offPicked = on('scene.picked', (e) => {
      if (e.id) {
        studioStore.select({ kind: 'object', id: e.id });
        showPanel('inspector');
      } else if (studioStore.get().selection?.kind === 'object') studioStore.select(null);
    });
    installEventSink();
    const off = () => {
      offLog();
      offEngine();
      offStats();
      offGraph();
      offProject();
      offEdit();
      offViewer();
      offPreview();
      offFloat();
      offShader();
      offScene();
      offPicked();
    };

    if (!isConnected()) {
      studioStore.setConnError('エディターに接続されていません。SunsetEditor の中で開いてください (STUDIO_UI_DEV_URL)。');
      return off;
    }

    (async () => {
      try {
        const ready = await call('ui.ready');
        if (ready.protocol !== PROTOCOL_VERSION) {
          studioStore.setConnError(`プロトコルのバージョンが違います (UI ${PROTOCOL_VERSION}, エディター ${ready.protocol})`);
        }
        studioStore.setInfo(await call('app.info'));
        studioStore.setProject(await call('project.info'));
        studioStore.setEdit(await call('edit.state'));
      } catch (e) {
        studioStore.setConnError(e instanceof StudioError ? `${e.message} (${e.code})` : String(e));
      }
    })();
    return off;
  }, []);

  const doPing = useCallback(async () => {
    const t0 = performance.now();
    try {
      await call('app.ping', { t: t0 });
      setPing(`${(performance.now() - t0).toFixed(1)} ms`);
    } catch (e) {
      setPing(String(e));
    }
  }, []);

  const project = useStudio((s) => s.project);
  const editState = useStudio((s) => s.edit);
  const viewer = useStudio((s) => s.viewer);
  const previews = useStudio((s) => s.previews).filter((p) => p.mode === 'window'); // タブのプレビューはパネルに出ている
  const floating = useStudio((s) => s.viewportFloating);

  // プロジェクトを開いたら、ビューアー / プレビュー / ビューポートの状態を読む (ツールバーに出す)
  useEffect(() => {
    if (!isConnected() || !project?.open) return;
    call('viewer.get').then(studioStore.setViewer).catch(() => {});
    call('preview.list').then((r) => studioStore.setPreviews(r.previews)).catch(() => {});
    call('viewport.state').then((r) => studioStore.setViewportFloating(r.floating)).catch(() => {});
    void loadShaderErrors();
    void loadScene();
  }, [project?.open, project?.file]);

  // 選んでいる物体をビューポートに伝える (辺の印)
  const selection = useStudio((s) => s.selection);
  const selectedObject = selection?.kind === 'object' ? selection.id : '';
  useEffect(() => {
    if (isConnected() && project?.open) void call('scene.select', { id: selectedObject }).catch(() => {});
  }, [selectedObject, project?.open]);

  // パイプラインが使うシェーダー (ノードの表示と右クリックのメニュー)。グラフが変わったら読み直す
  const graphPipelines = useStudio((s) => s.graph?.pipelines.join('\n') ?? '');
  useEffect(() => {
    if (isConnected() && project?.open && graphPipelines) void loadPipelineShaders(graphPipelines.split('\n'));
  }, [graphPipelines, project?.open]);
  const [closeError, setCloseError] = useState('');
  const [editError, setEditError] = useState('');

  const undoRedo = useCallback(async (undo: boolean) => {
    try {
      setEditError('');
      const r = await call(undo ? 'edit.undo' : 'edit.redo');
      studioStore.setEdit(r.state);
    } catch (e) {
      setEditError(errorText(e));
    }
  }, []);

  // Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z (文字を入力している欄では、ブラウザの取り消しを優先する)
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!e.ctrlKey || e.altKey || e.metaKey) return;
      const key = e.key.toLowerCase();
      if (key !== 'z' && key !== 'y') return;
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === 'TEXTAREA' || (t.tagName === 'INPUT' && !['checkbox', 'radio', 'button'].includes((t as HTMLInputElement).type)))) return;
      e.preventDefault();
      const undo = key === 'z' && !e.shiftKey;
      const state = studioStore.get().edit;
      if (undo ? state?.canUndo : state?.canRedo) void undoRedo(undo);   // 履歴がなければ何もしない
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [undoRedo]);
  // 古い構成のプロジェクトを、直下に並べる構成に移す (ファイルを動かすので確認する)
  const migrateProject = async () => {
    const ok = window.confirm(
      'Assets/ の中身 (graphs/ pipelines/ shaders/ など) をプロジェクトの直下に移し、Assets/ を消します。\n' +
        'プロジェクトを開き直すので、元に戻す / やり直すの履歴は消えます。\n' +
        '直下に同じ名前のフォルダがあれば、何も動かさずに中止します。\n\n移しますか？',
    );
    if (!ok) return;
    try {
      setEditError('');
      studioStore.setProject(await call('project.migrate'));
    } catch (e) {
      setEditError(errorText(e));
    }
  };

  const closeProject = async () => {
    try {
      setCloseError('');
      studioStore.setProject(await call('project.close'));
    } catch (e) {
      setCloseError(errorText(e));
    }
  };

  return (
    <div className="app">
      <header className="toolbar">
        <span className="brand">SunsetEditor</span>
        {project?.open && (
          <>
            <span className="project-name" title={project.file}>
              {project.name}
            </span>
            <button onClick={() => void closeProject()} title="プロジェクトを閉じて起動画面に戻る">
              閉じる
            </button>
            <button onClick={() => showPanel('scene')} title="シーンの物体の一覧 (配置はインスペクターで編集)">
              シーン
            </button>
            <button onClick={() => showPanel('nodes')} title="レンダーグラフをノードと線で編集する">
              ノードエディター
            </button>
            <button onClick={() => showPanel('viewer')} title="途中のテクスチャ (G-Buffer、深度など) を表示する">
              リソースビューアー
            </button>
            <button
              onClick={() =>
                floating
                  ? void call('viewport.float', { floating: false }).then((r) => studioStore.setViewportFloating(r.floating)).catch(() => {})
                  : void popoutPanel('viewport')
              }
              title={floating ? 'ビューポートをメインのウィンドウに戻す' : 'ビューポートを別ウィンドウにする (タブをウィンドウの外へドラッグしても同じ)'}
            >
              {floating ? 'ビューポートを戻す' : 'ビューポートを別ウィンドウに'}
            </button>
            <button
              onClick={() => void undoRedo(true)}
              disabled={!editState?.canUndo}
              title={editState?.canUndo ? `元に戻す: ${editState.undoLabel} (Ctrl+Z)` : '元に戻す編集はありません'}
            >
              ↶ 元に戻す
            </button>
            <button
              onClick={() => void undoRedo(false)}
              disabled={!editState?.canRedo}
              title={editState?.canRedo ? `やり直す: ${editState.redoLabel} (Ctrl+Y)` : 'やり直す編集はありません'}
            >
              ↷ やり直す
            </button>
          </>
        )}
        {project?.open && project.legacyLayout && (
          <span className="chip legacy" title="アセットが Assets/ の下にある古い構成です。scenes/ graphs/ shaders/ などをプロジェクトの直下に並べる構成に移せます">
            古い構成 (Assets/)
            <button onClick={() => void migrateProject()}>新しい構成に移す</button>
          </span>
        )}
        {project?.open && viewer?.enabled && viewer.resource && (
          <span className="chip viewing" title="ビューポートに、最終出力の代わりにこのリソースを表示しています">
            表示中: {viewer.resource}
            <button onClick={() => void setViewer({ enabled: false })} title="ビューポートを最終出力に戻す">
              ✕
            </button>
          </span>
        )}
        {project?.open && previews.length > 0 && (
          <span className="chip" title={previews.map((p) => p.resource + (p.status ? ` (${p.status})` : '')).join('\n')}>
            別ウィンドウ {previews.length}
          </span>
        )}
        {editError && <span className="status error" title={editError}>{editError.split('\n')[0]}</span>}
        {closeError && <span className="status error">{closeError}</span>}
        <span className="spacer" />
        {project?.open && (
          <span className="muted hint">ビューポート: 左ドラッグで回転 / 右・中ドラッグで移動 / ホイールでズーム / F で視点を戻す</span>
        )}
        <button onClick={doPing} disabled={!isConnected()}>
          Ping
        </button>
        {ping && <span className="muted">{ping}</span>}
      </header>

      {connError && <div className="banner error">{connError}</div>}

      <main className="workspace">
        {!isConnected() || project?.open ? (
          <Workspace key={project?.file ?? ''} />
        ) : project ? (
          <StartScreen />
        ) : (
          <div className="placeholder">接続しています…</div>
        )}
      </main>

      <StatusBar />
    </div>
  );
}

// プロジェクトのパネル。配置は Saved/Layout.json から戻し、変わったら保存する
function Workspace() {
  const [layout, setLayout] = useState<JsonValue | undefined>(isConnected() ? undefined : null);
  useEffect(() => {
    if (!isConnected()) return;
    call('project.layout.get')
      .then((r) => setLayout(r.layout ?? null))
      .catch(() => setLayout(null));
  }, []);
  if (layout === undefined) return <div className="placeholder">パネルの配置を読み込んでいます…</div>;

  const onReady = (event: DockviewReadyEvent) => {
    setDockApi(event.api);
    watchDragOut(event.api);
    let restored = false;
    if (layout && typeof layout === 'object') {
      try {
        event.api.fromJSON(layout as unknown as SerializedDockview);
        restored = event.api.panels.length > 0;
      } catch {
        restored = false;
      }
    }
    if (!restored) {
      event.api.clear();
      defaultLayout(event);
    }
    if (!isConnected()) return;
    let timer = 0;
    event.api.onDidLayoutChange(() => {
      window.clearTimeout(timer);
      timer = window.setTimeout(() => {
        void call('project.layout.set', { layout: event.api.toJSON() as unknown as JsonValue }).catch(() => {});
      }, 500);
    });
  };
  return <DockviewReact components={components} onReady={onReady} theme={themeDark} />;
}

function StatusBar() {
  const info = useStudio((s) => s.info);
  const engine = useStudio((s) => s.engine);
  const stats = useStudio((s) => s.stats);
  return (
    <footer className="statusbar">
      <EngineStatus engine={engine} />
      {stats && (
        <span className="muted">
          {stats.fps.toFixed(0)} fps · {stats.frameMs.toFixed(2)} ms (CPU {stats.cpuMs.toFixed(2)} ms) · {stats.width}×
          {stats.height}
        </span>
      )}
      <span className="spacer" />
      {info && (
        <span className="muted">
          {info.app} {info.version} · protocol {info.protocol} · ABI {engine?.abi ?? info.engine.abi}
        </span>
      )}
    </footer>
  );
}

function EngineStatus({ engine }: { engine: EngineInfo | null }) {
  if (!engine) return <span className="status">エンジン: 接続中…</span>;
  switch (engine.status) {
    case 'starting':
      return <span className="status">エンジン: 初期化中…</span>;
    case 'ready':
      return (
        <span className="status ok">
          エンジン: {engine.adapter} / {engine.api}
        </span>
      );
    case 'failed':
      return <span className="status error">エンジン: 初期化に失敗 ({engine.error})</span>;
  }
}
