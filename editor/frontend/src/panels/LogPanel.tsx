// ============================================================
//  ログのパネル: エンジンのデバッグメッセージ (シェーダーのエラー、D3D12 の検証) とアプリのメッセージ
// ============================================================
import { useEffect, useMemo, useRef, useState } from 'react';
import type { LogLevel } from '../bridge/protocol';
import { studioStore, useStudio } from '../state/store';

const LEVELS: LogLevel[] = ['error', 'warning', 'info'];
const LEVEL_LABEL: Record<LogLevel, string> = { error: 'エラー', warning: '警告', info: '情報' };

function formatTime(ms: number): string {
  const s = ms / 1000;
  return s.toFixed(3).padStart(8, ' ');
}

export function LogPanel() {
  const entries = useStudio((s) => s.logs);
  const [enabled, setEnabled] = useState<Record<LogLevel, boolean>>({ error: true, warning: true, info: true });
  const [query, setQuery] = useState('');
  const listRef = useRef<HTMLDivElement>(null);
  const stickToBottom = useRef(true);

  const counts = useMemo(() => {
    const c: Record<LogLevel, number> = { error: 0, warning: 0, info: 0 };
    for (const e of entries) c[e.level]++;
    return c;
  }, [entries]);

  const visible = useMemo(() => {
    const q = query.trim().toLowerCase();
    return entries.filter(
      (e) => enabled[e.level] && (!q || e.text.toLowerCase().includes(q) || e.source.includes(q)),
    );
  }, [entries, enabled, query]);

  // 一番下を見ているときだけ、新しい行に追従する
  useEffect(() => {
    const el = listRef.current;
    if (el && stickToBottom.current) el.scrollTop = el.scrollHeight;
  }, [visible]);

  return (
    <section className="panel log-panel">
      <header className="panel-header">
        {LEVELS.map((level) => (
          <label key={level} className={`log-toggle level-${level}`}>
            <input
              type="checkbox"
              checked={enabled[level]}
              onChange={(e) => setEnabled({ ...enabled, [level]: e.target.checked })}
            />
            {LEVEL_LABEL[level]} {counts[level]}
          </label>
        ))}
        <input
          className="log-search"
          placeholder="絞り込み"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
        />
        <button onClick={studioStore.clearLogs}>クリア</button>
      </header>
      <div
        className="log-list"
        ref={listRef}
        onScroll={(e) => {
          const el = e.currentTarget;
          stickToBottom.current = el.scrollHeight - el.scrollTop - el.clientHeight < 8;
        }}
      >
        {visible.map((e, i) => (
          <div key={i} className={`log-row level-${e.level}`}>
            <span className="log-time">{formatTime(e.timeMs)}</span>
            <span className="log-source">{e.source}</span>
            <span className="log-text">{e.text}</span>
          </div>
        ))}
        {visible.length === 0 && <div className="log-empty">メッセージはありません</div>}
      </div>
    </section>
  );
}
