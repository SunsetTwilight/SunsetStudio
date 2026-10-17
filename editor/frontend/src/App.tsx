// ============================================================
//  SunsetEditor の画面 (M2a: 骨格)
//
//    ┌ ツールバー ─────────────────────────────────────┐
//    │ アウトライナー │ ビューポート (M2b)  │ インスペクター (M2c) │
//    ├────────────────────────────────────────────────┤
//    │ ログ                                              │
//    └ ステータスバー (エンジンの状態) ─────────────────┘
// ============================================================
import { useCallback, useEffect, useState } from 'react';
import { call, installEventSink, isConnected, on, StudioError } from './bridge/client';
import { PROTOCOL_VERSION, type AppInfo, type EngineInfo, type LogEntry } from './bridge/protocol';
import { LogPanel } from './panels/LogPanel';

const MAX_LOG_ENTRIES = 5000;

export function App() {
  const [info, setInfo] = useState<AppInfo | null>(null);
  const [engine, setEngine] = useState<EngineInfo | null>(null);
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [connError, setConnError] = useState<string | null>(null);
  const [ping, setPing] = useState<string>('');

  const addLog = useCallback((entry: LogEntry) => {
    setLogs((prev) => {
      const next = prev.length >= MAX_LOG_ENTRIES ? prev.slice(prev.length - MAX_LOG_ENTRIES + 1) : prev.slice();
      next.push(entry);
      return next;
    });
  }, []);

  useEffect(() => {
    const offLog = on('log', addLog);
    const offEngine = on('engine.status', setEngine);
    installEventSink();

    if (!isConnected()) {
      setConnError('エディターに接続されていません。SunsetEditor の中で開いてください (STUDIO_UI_DEV_URL)。');
      return () => {
        offLog();
        offEngine();
      };
    }

    (async () => {
      try {
        const ready = await call('ui.ready');
        if (ready.protocol !== PROTOCOL_VERSION) {
          setConnError(`プロトコルのバージョンが違います (UI ${PROTOCOL_VERSION}, エディター ${ready.protocol})`);
        }
        const appInfo = await call('app.info');
        setInfo(appInfo);
        setEngine((current) => current ?? appInfo.engine);
      } catch (e) {
        setConnError(e instanceof StudioError ? `${e.message} (${e.code})` : String(e));
      }
    })();

    return () => {
      offLog();
      offEngine();
    };
  }, [addLog]);

  const doPing = async () => {
    const t0 = performance.now();
    try {
      await call('app.ping', { t: t0 });
      setPing(`${(performance.now() - t0).toFixed(1)} ms`);
    } catch (e) {
      setPing(String(e));
    }
  };

  return (
    <div className="app">
      <header className="toolbar">
        <span className="brand">SunsetEditor</span>
        <span className="spacer" />
        <button onClick={doPing} disabled={!isConnected()}>Ping</button>
        {ping && <span className="muted">{ping}</span>}
      </header>

      {connError && <div className="banner error">{connError}</div>}

      <main className="workspace">
        <section className="panel outliner">
          <header className="panel-header">
            <span className="panel-title">アウトライナー</span>
          </header>
          <div className="placeholder">シーンとレンダーグラフ (M3 以降)</div>
        </section>

        <section className="panel viewport-panel">
          <header className="panel-header">
            <span className="panel-title">ビューポート</span>
          </header>
          {/* M2b: この要素の矩形にエンジンの子ウィンドウを重ねる */}
          <div className="viewport" data-viewport>
            <div className="placeholder">3D ビューポート (M2b)</div>
          </div>
        </section>

        <section className="panel inspector">
          <header className="panel-header">
            <span className="panel-title">インスペクター</span>
          </header>
          <div className="placeholder">記述子のフォーム (M2c)</div>
        </section>

        <LogPanel entries={logs} onClear={() => setLogs([])} />
      </main>

      <footer className="statusbar">
        <EngineStatus engine={engine} />
        <span className="spacer" />
        {info && (
          <span className="muted">
            {info.app} {info.version} · protocol {info.protocol} · ABI {engine?.abi ?? info.engine.abi}
          </span>
        )}
      </footer>
    </div>
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
