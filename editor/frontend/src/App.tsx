// ============================================================
//  SunsetEditor の画面
//
//    ┌ ツールバー ───────────────────────────────────────┐
//    │ dockview: レンダーグラフ │ ビューポート │ インスペクター  │
//    │           ログ (ビューポートの下)                       │
//    └ ステータスバー (エンジンの状態、フレームの統計) ──────┘
//
//  パネルはドラッグで移動 / 分割 / タブにできる (dockview)。配置はプロジェクトの Saved/Layout.json に残す。
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
import { ViewportPanel } from './panels/ViewportPanel';
import { errorText, loadGraph } from './state/graph';
import { studioStore, useStudio } from './state/store';

const components: Record<string, React.FunctionComponent<IDockviewPanelProps>> = {
  viewport: () => <ViewportPanel />,
  graph: () => <GraphPanel />,
  inspector: () => <InspectorPanel />,
  log: () => <LogPanel />,
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
    id: 'inspector',
    component: 'inspector',
    title: 'インスペクター',
    position: { referencePanel: 'viewport', direction: 'right' },
    initialWidth: 380,
  });
  api.addPanel({
    id: 'log',
    component: 'log',
    title: 'ログ',
    position: { referencePanel: 'viewport', direction: 'below' },
    initialHeight: 220,
  });
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
    installEventSink();
    const off = () => {
      offLog();
      offEngine();
      offStats();
      offGraph();
      offProject();
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
  const [closeError, setCloseError] = useState('');
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
          </>
        )}
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
