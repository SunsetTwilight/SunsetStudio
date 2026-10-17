// ============================================================
//  SunsetEditor の画面
//
//    ┌ ツールバー ───────────────────────────────────────┐
//    │ dockview: アウトライナー │ ビューポート │ インスペクター  │
//    │           ログ (ビューポートの下)                       │
//    └ ステータスバー (エンジンの状態、フレームの統計) ──────┘
//
//  パネルはドラッグで移動 / 分割 / タブにできる (dockview)。
//  ビューポートの上には C++ の子ウィンドウが重なる (ViewportPanel)。
// ============================================================
import { useCallback, useEffect, useState } from 'react';
import { DockviewReact, themeDark, type DockviewReadyEvent, type IDockviewPanelProps } from 'dockview-react';
import 'dockview-react/dist/styles/dockview.css';
import { call, installEventSink, isConnected, on, StudioError } from './bridge/client';
import { PROTOCOL_VERSION, type EngineInfo } from './bridge/protocol';
import { InspectorPanel } from './panels/InspectorPanel';
import { LogPanel } from './panels/LogPanel';
import { ViewportPanel } from './panels/ViewportPanel';
import { studioStore, useStudio } from './state/store';

function Placeholder({ text }: { text: string }) {
  return (
    <div className="panel">
      <div className="placeholder">{text}</div>
    </div>
  );
}

const components: Record<string, React.FunctionComponent<IDockviewPanelProps>> = {
  viewport: () => <ViewportPanel />,
  outliner: () => <Placeholder text="シーンとレンダーグラフ (M3 以降)" />,
  inspector: () => <InspectorPanel />,
  log: () => <LogPanel />,
};

// 初期のレイアウト
function onReady(event: DockviewReadyEvent) {
  const api = event.api;
  api.addPanel({ id: 'viewport', component: 'viewport', title: 'ビューポート' });
  api.addPanel({
    id: 'outliner',
    component: 'outliner',
    title: 'アウトライナー',
    position: { referencePanel: 'viewport', direction: 'left' },
    initialWidth: 240,
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
    installEventSink();
    const off = () => {
      offLog();
      offEngine();
      offStats();
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

  return (
    <div className="app">
      <header className="toolbar">
        <span className="brand">SunsetEditor</span>
        <span className="spacer" />
        <span className="muted hint">ビューポート: 左ドラッグで回転 / 右・中ドラッグで移動 / ホイールでズーム / F で視点を戻す</span>
        <button onClick={doPing} disabled={!isConnected()}>
          Ping
        </button>
        {ping && <span className="muted">{ping}</span>}
      </header>

      {connError && <div className="banner error">{connError}</div>}

      <main className="workspace">
        <DockviewReact components={components} onReady={onReady} theme={themeDark} />
      </main>

      <StatusBar />
    </div>
  );
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
