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
import { useEffect, useState, useRef } from 'react';
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
import { LibraryPanel } from './panels/LibraryPanel';
import { loadLibrary } from './state/library';
import { loadScene } from './state/scene';
import { loadPipelineShaders, loadShaderErrors } from './state/assets';
import { ViewerPanel } from './panels/ViewerPanel';
import { TitleBar } from './panels/TitleBar';
import { UnsavedDialog } from './panels/UnsavedDialog';
import { undoRedo, saveAll, migrateMaterials } from './state/commands';
import { NodeEditorPanel } from './nodegraph/NodeEditorPanel';
import { ViewportPanel } from './panels/ViewportPanel';
import { defaultLayout, setDockApi, showPanel, watchDragOut } from './state/dock';
import { loadGraph } from './state/graph';
import { studioStore, useStudio } from './state/store';

const components: Record<string, React.FunctionComponent<IDockviewPanelProps>> = {
  viewport: () => <ViewportPanel />,
  graph: () => <GraphPanel />,
  inspector: () => <InspectorPanel />,
  log: () => <LogPanel />,
  viewer: () => <ViewerPanel />,
  nodes: () => <NodeEditorPanel />,
  scene: () => <ScenePanel />,
  library: () => <LibraryPanel />,
  preview: (props) => <PreviewPanel {...(props as IDockviewPanelProps<PreviewParams>)} />,
  asset: (props) => <AssetPanel {...(props as IDockviewPanelProps<AssetParams>)} />,
  pipeline: (props) => <PipelinePanel {...(props as IDockviewPanelProps<PipelineParams>)} />,
};

export function App() {
  const connError = useStudio((s) => s.connError);

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
    const offUnsaved = on('unsaved.changed', studioStore.setUnsaved);
    const offCloseReq = on('app.closeRequested', (e) => {
      studioStore.setUnsaved(e);
      studioStore.setClosePrompt('window');
    });
    const offShader = on('shader.status', (e) => studioStore.setShaderStatus(e.path, e.ok, e.text));
    // シーン: 元に戻す / やり直す / 保存で変わったら読み直す。ビューポートのクリックで選ぶ
    const offScene = on('scene.changed', (e) => {
      const cur = studioStore.get().scene;
      if (!cur || cur.revision !== e.revision) void loadScene();
      else if (cur.dirty !== e.dirty) studioStore.setScene({ ...cur, dirty: e.dirty });
    });
    // ライブラリ: 取り込み / 読み込みの状態が変わったら読み直す
    const offLibrary = on('library.changed', () => void loadLibrary());
    // マテリアルのファイルが増えた / 変わった (一覧を読み直す。タブは自分で読み直す)
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
      offUnsaved();
      offCloseReq();
      offShader();
      offScene();
      offPicked();
      offLibrary();
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

  const project = useStudio((s) => s.project);

  // プロジェクトを開いたら、ビューアー / プレビュー / ビューポートの状態を読む (タイトルバーに出す)
  useEffect(() => {
    if (!isConnected() || !project?.open) return;
    call('viewer.get').then(studioStore.setViewer).catch(() => {});
    call('preview.list').then((r) => studioStore.setPreviews(r.previews)).catch(() => {});
    call('viewport.state').then((r) => studioStore.setViewportFloating(r.floating)).catch(() => {});
    call('project.unsaved').then(studioStore.setUnsaved).catch(() => {});
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
  // マテリアル (M5-4) のころのプロジェクトを開いたら、移すか一度だけ聞く (あとからはタイトルバー / メニューから)
  const migration = useStudio((s) => s.project?.materialMigration);
  const askedMigration = useRef('');
  useEffect(() => {
    const file = studioStore.get().project?.file ?? '';
    if (!migration?.changes.length || askedMigration.current === file) return;
    askedMigration.current = file;
    void migrateMaterials();
  }, [migration]);

  // Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z (文字を入力している欄では、ブラウザの取り消しを優先する)
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!e.ctrlKey || e.altKey || e.metaKey) return;
      const key = e.key.toLowerCase();
      // Ctrl+S: すべて保存 (コードエディターの中では、そのファイルの保存を優先する)
      if (key === 's' && !e.shiftKey) {
        if ((e.target as HTMLElement | null)?.closest?.('.monaco-editor')) return;
        e.preventDefault();
        void saveAll();
        return;
      }
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
  }, []);
  return (
    <div className="app">
      <TitleBar />
      <UnsavedDialog />

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
      defaultLayout(event.api);
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
