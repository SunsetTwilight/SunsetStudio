// ============================================================
//  レンダーグラフのパネル (アウトライナー): ビューポートを描くグラフのパスとリソース
//
//    パス: 宣言順に並べ、実行順の番号、パスごとの GPU 時間 (棒の長さ)、実行しない理由を表示する。
//          チェックボックスで有効 / 無効を切り替える (すぐビューポートに反映される)。
//    リソース: フォーマットと大きさ。
//    クリックすると、インスペクターでパス / リソースを編集できる。
//    保存: グラフのアセット (graphs/viewport.json) に書く。ファイルを直接編集しても、その場で反映される。
// ============================================================
import { useEffect, useMemo, useState } from 'react';
import { call, isConnected } from '../bridge/client';
import type { JsonObject } from '../bridge/protocol';
import { applyGraph, asObject, errorText, loadGraph, passesOf, withPass } from '../state/graph';
import { studioStore, useStudio } from '../state/store';

const SKIP_LABEL: Record<string, string> = { disabled: '無効', condition: '条件が偽', culled: '結果に届かない' };

type Status = { kind: 'idle' | 'busy' | 'ok' | 'error'; text: string };

export function GraphPanel() {
  const graph = useStudio((s) => s.graph);
  const stats = useStudio((s) => s.stats);
  const selection = useStudio((s) => s.selection);
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });
  const engineReady = useStudio((s) => s.engine?.status === 'ready');

  // エンジンの準備ができたら読む (以後は graph.changed のイベントで読み直す)
  useEffect(() => {
    if (isConnected() && engineReady && !graph) void loadGraph().catch((e) => setStatus({ kind: 'error', text: errorText(e) }));
  }, [engineReady, graph]);

  // パスごとの GPU 時間 (繰り返したパスは "Name[k]" なので、元の名前でまとめる)
  const timing = useMemo(() => {
    const m = new Map<string, number>();
    for (const t of stats?.passes ?? []) {
      const base = t.name.replace(/\[\d+\]$/, '');
      m.set(base, (m.get(base) ?? 0) + t.gpuMs);
    }
    return m;
  }, [stats]);
  const maxMs = Math.max(0.001, ...timing.values());

  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;
  if (!graph) return <div className="panel"><div className="placeholder">グラフを読み込んでいます…</div></div>;

  const g = graph.graph;
  const passes = passesOf(g);
  const resources = asObject(g.resources);
  const skipped = new Map(graph.skipped.map((s) => [s.name, s.reason]));

  const apply = async (next: JsonObject, what: string) => {
    setStatus({ kind: 'busy', text: `${what}…` });
    const err = await applyGraph(next);
    setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: what });
  };

  const save = async () => {
    setStatus({ kind: 'busy', text: '保存しています…' });
    try {
      await call('graph.save');
      setStatus({ kind: 'ok', text: `保存しました (${graph.file})` });
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  };

  return (
    <div className="panel graph-panel">
      <header className="panel-header">
        <span className="panel-title" title={graph.file}>
          {graph.file}
        </span>
        <span className="muted">rev {graph.revision}</span>
        <span className="spacer" />
        <button onClick={() => void loadGraph()} title="エディターの今のグラフを読み直す">
          更新
        </button>
        <button onClick={() => void save()} title="グラフのアセットに書く">
          保存
        </button>
      </header>
      {status.text && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}

      <div className="graph-body">
        <div className="graph-section">
          パス
          <span className="muted">
            {' '}
            · GPU {stats ? `${stats.gpuMs.toFixed(3)} ms` : '-'}
          </span>
        </div>
        {passes.map((p) => {
          const name = String(p.name ?? '');
          const index = graph.order.indexOf(name);
          const reason = skipped.get(name);
          const ms = timing.get(name);
          const selected = selection?.kind === 'pass' && selection.name === name;
          return (
            <div
              key={name}
              className={`graph-row${selected ? ' selected' : ''}${reason ? ' skipped' : ''}`}
              onClick={() => studioStore.select({ kind: 'pass', name })}
            >
              <input
                type="checkbox"
                checked={p.enabled !== false}
                title="有効 / 無効"
                onClick={(e) => e.stopPropagation()}
                onChange={(e) =>
                  void apply(
                    withPass(g, name, (q) => ({ ...q, enabled: e.target.checked })),
                    `${name} を${e.target.checked ? '有効' : '無効'}にしました`,
                  )
                }
              />
              <span className="graph-order">{index >= 0 ? `#${index + 1}` : ''}</span>
              <span className="graph-name">{name}</span>
              {reason ? (
                <span className="graph-chip">{SKIP_LABEL[reason] ?? reason}</span>
              ) : (
                <span className="graph-time" title="GPU 時間 (タイムスタンプ)">
                  <span className="graph-bar" style={{ width: `${((ms ?? 0) / maxMs) * 100}%` }} />
                  <span className="graph-ms">{ms !== undefined ? ms.toFixed(3) : '-'}</span>
                </span>
              )}
            </div>
          );
        })}

        <div className="graph-section">リソース</div>
        {Object.entries(resources).map(([name, v]) => {
          const r = asObject(v);
          const info = graph.resources.find((x) => x.name === name);
          const selected = selection?.kind === 'resource' && selection.name === name;
          const size = asObject(r.size);
          const sizeText = size.relative !== undefined ? `×${size.relative}` : size.width !== undefined ? `${size.width}×${size.height}` : '';
          return (
            <div
              key={name}
              className={`graph-row${selected ? ' selected' : ''}${info && !info.used ? ' skipped' : ''}`}
              onClick={() => studioStore.select({ kind: 'resource', name })}
            >
              <span className="graph-name">{name}</span>
              <span className="muted graph-format">
                {String(r.kind ?? r.format ?? '')} {sizeText}
              </span>
            </div>
          );
        })}

        {graph.diagnostics.length > 0 && <div className="graph-section">コンパイルの診断</div>}
        {graph.diagnostics.map((d, i) => (
          <div key={i} className={`graph-diag level-${d.level}`} title={d.path}>
            {d.path}: {d.text}
          </div>
        ))}
      </div>
    </div>
  );
}
