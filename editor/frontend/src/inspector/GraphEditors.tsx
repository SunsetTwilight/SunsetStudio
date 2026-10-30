// ============================================================
//  インスペクター: レンダーグラフのパスとリソースの編集
//
//    PassEditor     有効 / 無効、パイプライン、順序、入力と出力 (定義名、使い方、load、bind)。
//                   どのリソースとつなぐかはノードエディターで決める (ここは表示だけ。未接続も可)
//                   入力 / 出力ごとに、届く先のシェーダーの変数 (→ t0 gAlbedo / → SV_Target1) とずれの警告 (pass.linkage)。
//                   宣言がなければ「宣言を作る」(pass.declare)、どの入力もつないでいない変数は「入力を作る」で未接続の入力を足す
//    ResourceEditor フォーマット、大きさ、mip、サンプル数
//  変えるたびにグラフ全体を graph.set で送り、エディターがコンパイルし直す (ビューポートにすぐ反映される)。
//  コンパイルできない変更はエラー (理由とパス付き) になり、ビューポートは前のグラフのまま。
// ============================================================
import { useEffect, useState } from 'react';
import { call, on } from '../bridge/client';
import type { GraphInfo, JsonObject, JsonValue, PassLinkage, PassLinkRef } from '../bridge/protocol';
import {
  applyGraph,
  asArray,
  asObject,
  findPass,
  withPass,
  withResource,
  applyInputGraph,
  connectShaderVar,
  errorText,
} from '../state/graph';
import { studioStore } from '../state/store';
import { showInViewer } from '../panels/ViewerPanel';
import { renamePass, renameResource, addInputPort, addOutputPort, renamePort, generatedTexture } from '../nodegraph/model';
import { openPreviewTab } from '../state/dock';

// 名前の欄 (Enter かフォーカスを外したときに確定する)
export function NameBox({ value, onCommit }: { value: string; onCommit: (v: string) => void }) {
  const [text, setText] = useState(value);
  useEffect(() => setText(value), [value]);
  const commit = () => {
    const v = text.trim();
    if (v && v !== value) onCommit(v);
    else setText(value);
  };
  return <input value={text} onChange={(e) => setText(e.target.value)} onBlur={commit} onKeyDown={(e) => e.key === 'Enter' && commit()} />;
}

type Status = { kind: 'idle' | 'busy' | 'ok' | 'error'; text: string };

const INPUT_AS = ['SRV', 'UAV', 'DSVRead', 'CopySrc', 'IndirectArgs', 'ResolveSrc'];
const OUTPUT_AS = ['RTV0', 'RTV1', 'RTV2', 'RTV3', 'RTV4', 'RTV5', 'RTV6', 'RTV7', 'DSV', 'UAV', 'CopyDst', 'ResolveDst'];
const LOADS = ['load', 'clear', 'dontCare'];
const NEEDS_BIND = new Set(['SRV', 'UAV']);

// GfxFormat の名前 (スキーマから 1 回だけ取る)
let formatNames: string[] | null = null;
async function loadFormats(): Promise<string[]> {
  if (formatNames) return formatNames;
  const d = await call('schema.describe', { type: 'GfxGraphicsPipelineDesc' });
  formatNames = (d.enums.GfxFormat?.values ?? []).map((v) => v.name);
  return formatNames;
}

function useApply(subject: string) {
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });
  const apply = async (next: JsonObject, what: string) => {
    setStatus({ kind: 'busy', text: `${what}…` });
    const err = await applyGraph(next, `${subject}: ${what}`);
    setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: `${what} (反映しました)` });
  };
  return { status, apply };
}

function StatusLine({ status }: { status: Status }) {
  return status.text ? <div className={`inspector-status ${status.kind}`}>{status.text}</div> : null;
}

function Select({ value, options, onChange }: { value: string; options: string[]; onChange: (v: string) => void }) {
  const all = options.includes(value) || value === '' ? options : [value, ...options];
  return (
    <select value={value} onChange={(e) => onChange(e.target.value)}>
      {all.map((o) => (
        <option key={o} value={o}>
          {o}
        </option>
      ))}
    </select>
  );
}

export function NumberBox({ value, min, step, onCommit }: { value: number; min?: number; step?: number; onCommit: (v: number) => void }) {
  const [text, setText] = useState(String(value));
  useEffect(() => setText(String(value)), [value]);
  const commit = () => {
    const v = Number(text);
    if (Number.isFinite(v) && v !== value) onCommit(v);
    else setText(String(value));
  };
  return (
    <input
      className="number"
      type="number"
      value={text}
      min={min}
      step={step ?? 1}
      onChange={(e) => setText(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => e.key === 'Enter' && commit()}
    />
  );
}

// パスの入力 / 出力とシェーダーの宣言のつながり。グラフが変わったとき、シェーダーをコンパイルし直したとき、
// パイプラインが変わったときに読み直す
function usePassLinkage(pass: string, revision: number) {
  const [link, setLink] = useState<PassLinkage | null>(null);
  const [tick, setTick] = useState(0);
  useEffect(() => {
    const bump = () => setTick((t) => t + 1);
    const offs = [on('shader.status', bump), on('shader.reloaded', bump), on('pipeline.changed', bump)];
    return () => offs.forEach((off) => off());
  }, []);
  useEffect(() => {
    let alive = true;
    call('pass.linkage', { pass })
      .then((l) => alive && setLink(l))
      .catch(() => alive && setLink(null));
    return () => {
      alive = false;
    };
  }, [pass, revision, tick]);
  return { link, refresh: () => setTick((t) => t + 1) };
}

// 入力 / 出力の行の下の 1 行: 届く先の変数か、ずれの警告
function LinkLine({ link, output, onDeclare }: { link?: PassLinkRef; output: boolean; onDeclare: () => void }) {
  if (!link || link.status === 'none') return null;
  const where = link.target !== undefined ? `SV_Target${link.target}` : (link.register ?? '');
  const file = link.var?.file ? `${link.var.file}:${link.var.line}` : '';
  if (link.status === 'ok')
    return (
      <div className="link-line ok" title={file}>
        → {where} {link.var?.name !== where ? link.var?.name : ''}
      </div>
    );
  if (link.status === 'unused')
    return (
      <div className="link-line warn" title={file}>
        ⚠ {where} {link.var?.name} を宣言していますが、シェーダーで使っていません
      </div>
    );
  if (link.status === 'noSlot')
    return <div className="link-line warn">⚠ bind がパイプラインのテーブルの外です (パイプラインに場所がありません)</div>;
  return (
    <div className="link-line warn">
      ⚠ {where} の宣言がシェーダーにありません
      <button onClick={onDeclare} title={output && link.target !== undefined ? 'PS の出力の構造体にメンバーを足す' : 'シェーダーに宣言を書き足す'}>
        宣言を作る
      </button>
    </div>
  );
}

// ============================================================
//  パス
// ============================================================
export function PassEditor({ graph, name }: { graph: GraphInfo; name: string }) {
  const { status, apply } = useApply(`パス ${name}`);
  const [renameError, setRenameError] = useState('');
  const { link, refresh } = usePassLinkage(name, graph.revision);
  const [linkStatus, setLinkStatus] = useState('');
  const g = graph.graph;
  const pass = findPass(g, name);
  if (!pass) return <div className="placeholder">パス '{name}' はありません</div>;

  const executed = graph.order.indexOf(name);
  const skipped = graph.skipped.find((s) => s.name === name);
  const diags = graph.diagnostics.filter((d) => d.path.startsWith(`passes[${name}]`));
  const update = (patch: (p: JsonObject) => JsonObject, what: string) => void apply(withPass(g, name, patch), what);

  const declare = (kind: 'input' | 'output', index: number) => {
    setLinkStatus('');
    call('pass.declare', { pass: name, kind, index })
      .then((d) => {
        setLinkStatus(`${d.file} の ${d.line} 行目に ${d.name} を書き足しました`);
        refresh();
      })
      .catch((e) => setLinkStatus(`エラー: ${errorText(e)}`));
  };

  const refRows = (key: 'inputs' | 'outputs') => {
    const refs = asArray(pass[key]).map(asObject);
    const links = key === 'inputs' ? link?.inputs : link?.outputs;
    const setRef = (i: number, ref: JsonObject, what: string) =>
      update((p) => ({ ...p, [key]: refs.map((r, k) => (k === i ? ref : r)) }), what);
    const removeRef = (i: number) => update((p) => ({ ...p, [key]: refs.filter((_, k) => k !== i) }), `${key}[${i}] を削除`);
    // 未接続の入力 / 出力を加える (定義名はエディターがシェーダーの変数名から入れる。つなぐのはノードエディター)
    const addRef = () => {
      const r = key === 'inputs' ? addInputPort(g, name) : addOutputPort(g, name);
      if ('error' in r) return void setRenameError(r.error);
      setRenameError('');
      if (key === 'outputs') return void apply(r.graph, r.label);
      // 入力: パイプラインのテーブルが足りなければ広げてから送る (シェーダーで使うには Texture2D の宣言が要る)
      void applyInputGraph(r.graph, name, r.label).then((err) =>
        setRenameError(err ?? ''),
      );
    };
    return (
      <div className="ref-table">
        <div className="ref-head">
          <span>{key === 'inputs' ? '入力' : '出力'}</span>
          <span className="spacer" />
          <button onClick={addRef}>追加</button>
        </div>
        {refs.length === 0 && <div className="muted form-empty">なし</div>}
        {refs.map((r, i) => {
          const as = String(r.as ?? '');
          const bind = asObject(r.bind);
          const path = `${key}[${i}]`;
          const res = typeof r.res === 'string' ? r.res : '';
          const portLink = links?.find((l) => l.index === i);
          const gen = key === 'outputs' ? generatedTexture(name, r, i) : null;
          const resFormat = res ? String(asObject(asObject(g.resources)[res]).format ?? '') : '';
          return (
            <div key={i} className="ref-row linked">
              <div className="ref-fields">
                <span className="ref-name" title="定義名 (シェーダーの変数名が既定)">
                  <NameBox
                    value={String(r.name ?? '')}
                    onCommit={(to) => {
                      const next = renamePort(g, name, key, i, to);
                      if (typeof next === 'string') return setRenameError(next);
                      setRenameError('');
                      void apply(next, `${path}.name = ${to}`);
                    }}
                  />
                </span>
                <Select
                  value={as}
                  options={key === 'inputs' ? INPUT_AS : OUTPUT_AS}
                  onChange={(v) => {
                    const next: JsonObject = { ...r, as: v };
                    if (NEEDS_BIND.has(v) && !r.bind) next.bind = { param: 0, offset: 0 };
                    if (!NEEDS_BIND.has(v)) delete next.bind;
                    setRef(i, next, `${path}.as = ${v}`);
                  }}
                />
                {key === 'outputs' && (
                  <Select value={String(r.load ?? 'load')} options={LOADS} onChange={(v) => setRef(i, { ...r, load: v }, `${path}.load = ${v}`)} />
                )}
                {NEEDS_BIND.has(as) && (
                  <span className="ref-bind" title="bind: ルートパラメーターの番号 / テーブルの中の位置">
                    param
                    <NumberBox value={Number(bind.param ?? 0)} min={0} onCommit={(v) => setRef(i, { ...r, bind: { ...bind, param: v } }, `${path}.bind.param = ${v}`)} />
                    offset
                    <NumberBox value={Number(bind.offset ?? 0)} min={0} onCommit={(v) => setRef(i, { ...r, bind: { ...bind, offset: v } }, `${path}.bind.offset = ${v}`)} />
                  </span>
                )}
                <button className="ref-remove" title="削除" onClick={() => removeRef(i)}>
                  ×
                </button>
              </div>
              <div className={`ref-link ${res ? '' : 'unconnected'}`} title="つなぐ / 外すのはノードエディターで">
                {res
                  ? key === 'inputs'
                    ? `← ${res}`
                    : `→ ${res}`
                  : key === 'inputs'
                    ? '未接続 (0 を読む)'
                    : gen
                      ? `未接続 (パス専用のテクスチャ ${gen.resource} に書く)`
                      : '未接続'}
                {key === 'outputs' && (gen || (res && !res.startsWith('$') && resFormat)) && (
                  <button
                    title="この出力の結果をタブで表示する"
                    onClick={() => (gen ? openPreviewTab(gen.resource, gen.format) : openPreviewTab(res, resFormat))}
                  >
                    表示
                  </button>
                )}
              </div>
              {portLink && (portLink.type || portLink.format) && (
                <div className="ref-type" title="型: 届く先のシェーダーの変数 / SV_Target の型、フォーマット: つないだリソース (未接続の出力は作るテクスチャ)">
                  型 <code>{portLink.type ?? '?'}</code> ・ フォーマット <code>{portLink.format ?? '?'}</code>
                </div>
              )}
              {portLink?.mismatch && <div className="ref-type mismatch">⚠ {portLink.mismatch}</div>}
              <LinkLine link={portLink} output={key === 'outputs'} onDeclare={() => declare(key === 'inputs' ? 'input' : 'output', i)} />
            </div>
          );
        })}
      </div>
    );
  };

  const pipeline = String(pass.pipeline ?? '');
  const constants = asArray(pass.constants).map(asObject);
  const content = asObject(pass.content);

  return (
    <div className="graph-editor">
      <StatusLine status={status} />
      <div className="form-row">
        <span className="form-label">名前</span>
        <span className="form-control">
          <NameBox
            value={name}
            onCommit={(to) => {
              const next = renamePass(g, name, to);
              if (typeof next === 'string') return setRenameError(next);
              setRenameError('');
              void apply(next, `名前を ${to} に変更`).then(() => studioStore.select({ kind: 'pass', name: to }));
            }}
          />
        </span>
      </div>
      {renameError && <div className="inspector-status error">{renameError}</div>}
      <div className="form-row">
        <span className="form-label">状態</span>
        <span className="form-control">
          {skipped ? `実行しない (${skipped.reason})` : executed >= 0 ? `${executed + 1} 番目に実行` : '-'}
        </span>
      </div>
      <div className="form-row">
        <span className="form-label">有効</span>
        <span className="form-control">
          <input type="checkbox" checked={pass.enabled !== false} onChange={(e) => update((p) => ({ ...p, enabled: e.target.checked }), `enabled = ${e.target.checked}`)} />
        </span>
      </div>
      <div className="form-row">
        <span className="form-label">種類</span>
        <span className="form-control">{String(pass.type ?? 'Raster')}</span>
      </div>
      {pass.type !== 'Copy' && (
        <div className="form-row">
          <span className="form-label">パイプライン</span>
          <span className="form-control">
            <Select value={pipeline} options={graph.pipelines} onChange={(v) => update((p) => ({ ...p, pipeline: v }), `pipeline = ${v}`)} />
            <button onClick={() => studioStore.select({ kind: 'pipeline', id: pipeline })} disabled={!pipeline}>
              編集
            </button>
          </span>
        </div>
      )}
      <div className="form-row" title="データの依存がないパスどうしは、小さいほど先に実行する">
        <span className="form-label">order</span>
        <span className="form-control">
          <NumberBox value={Number(pass.order ?? 0)} onCommit={(v) => update((p) => ({ ...p, order: v }), `order = ${v}`)} />
        </span>
      </div>
      <div className="form-row">
        <span className="form-label">内容</span>
        <span className="form-control muted">{JSON.stringify(content)}</span>
      </div>
      {refRows('inputs')}
      {refRows('outputs')}
      {link?.error && <div className="link-line warn">{link.error}</div>}
      {linkStatus && <div className={`inspector-status ${linkStatus.startsWith('エラー') ? 'error' : 'ok'}`}>{linkStatus}</div>}
      {link && ((link.unconnected?.length ?? 0) > 0 || (link.targets ?? []).some((t) => !t.connected)) && (
        <div className="ref-table">
          <div className="ref-head" title="シェーダーが宣言しているが、このパスのどの入力 / 出力もつないでいないもの">
            <span>つながっていないシェーダーの変数</span>
          </div>
          {(link.unconnected ?? []).map((d) => {
            const key = `${d.cls}${d.reg}/${d.space}`;
            return (
              <div key={key} className="ref-row">
                <span className="link-var" title={d.file ? `${d.file}:${d.line}` : 'リフレクションで分かったもの'}>
                  {d.type} <b>{d.name}</b> : {d.register}
                  {!d.used && <span className="muted"> (未使用)</span>}
                </span>
                {d.cls === 't' && d.space === 0 ? (
                  <button
                    title="この変数を読む未接続の入力 (定義名は変数名) を足し、パイプラインにこのレジスタを指す場所を作る。リソースはノードエディターでつなぐ"
                    onClick={() => {
                      setLinkStatus('');
                      void connectShaderVar(g, name, d.name, d.reg, `パス ${name}: ${d.name} (${d.register}) の入力を作る`).then((err) =>
                        setLinkStatus(err ? `エラー: ${err}` : `入力 ${d.name} を作りました (ノードエディターでリソースをつないでください)`),
                      );
                    }}
                  >
                    入力を作る
                  </button>
                ) : (
                  <span className="muted">(UAV / space 付きは手でつないでください)</span>
                )}
              </div>
            );
          })}
          {(link.targets ?? [])
            .filter((t) => !t.connected)
            .map((t) => (
              <div key={`sv${t.index}`} className="ref-row">
                <span className="link-var">
                  SV_Target{t.index} {t.name && <b>{t.name}</b>}
                </span>
                <span className="muted">どの出力 (RTV{t.index}) も書いていません</span>
              </div>
            ))}
        </div>
      )}
      <div className="ref-table">
        <div className="ref-head">
          <span>定数</span>
        </div>
        {constants.length === 0 && <div className="muted form-empty">なし</div>}
        {constants.map((c, i) => (
          <div key={i} className="ref-row muted">
            {String(c.source ?? '')} → param {String(asObject(c.bind).param ?? '-')}
          </div>
        ))}
      </div>
      {diags.map((d, i) => (
        <div key={i} className={`graph-diag level-${d.level}`}>
          {d.path}: {d.text}
        </div>
      ))}
    </div>
  );
}

// ============================================================
//  リソース
// ============================================================
export function ResourceEditor({ graph, name }: { graph: GraphInfo; name: string }) {
  const { status, apply } = useApply(`リソース ${name}`);
  const [renameError, setRenameError] = useState('');
  const [formats, setFormats] = useState<string[]>(formatNames ?? []);
  useEffect(() => {
    void loadFormats().then(setFormats).catch(() => setFormats([]));
  }, []);

  const g = graph.graph;
  const res = asObject(asObject(g.resources)[name]);
  if (!asObject(g.resources)[name]) return <div className="placeholder">リソース '{name}' はありません</div>;
  const info = graph.resources.find((r) => r.name === name);
  const kind = String(res.kind ?? 'texture');
  const size = asObject(res.size);
  const relative = size.relative !== undefined;
  const update = (patch: (r: JsonObject) => JsonObject, what: string) => void apply(withResource(g, name, patch), what);
  const setField = (key: string, value: JsonValue, what: string) => update((r) => ({ ...r, [key]: value }), what);
  const users = graph.order.filter((_, i) => info && info.used && i >= info.firstPass && i <= info.lastPass);

  return (
    <div className="graph-editor">
      <StatusLine status={status} />
      <div className="form-row">
        <span className="form-label">名前</span>
        <span className="form-control">
          <NameBox
            value={name}
            onCommit={(to) => {
              const next = renameResource(g, name, to);
              if (typeof next === 'string') return setRenameError(next);
              setRenameError('');
              void apply(next, `名前を ${to} に変更`).then(() => studioStore.select({ kind: 'resource', name: to }));
            }}
          />
        </span>
      </div>
      {renameError && <div className="inspector-status error">{renameError}</div>}
      <div className="form-row">
        <span className="form-label">種類</span>
        <span className="form-control">{kind}</span>
      </div>
      {kind === 'texture' && (
        <>
          <div className="form-row">
            <span className="form-label">format</span>
            <span className="form-control">
              <Select value={String(res.format ?? '')} options={formats} onChange={(v) => setField('format', v, `format = ${v}`)} />
            </span>
          </div>
          <div className="form-row">
            <span className="form-label">大きさ</span>
            <span className="form-control">
              {relative ? (
                <>
                  出力の ×
                  <NumberBox value={Number(size.relative)} min={0.0625} step={0.25} onCommit={(v) => setField('size', { relative: v }, `size.relative = ${v}`)} />
                </>
              ) : (
                <>
                  <NumberBox value={Number(size.width ?? 1)} min={1} onCommit={(v) => setField('size', { ...size, width: v }, `size.width = ${v}`)} />×
                  <NumberBox value={Number(size.height ?? 1)} min={1} onCommit={(v) => setField('size', { ...size, height: v }, `size.height = ${v}`)} />
                </>
              )}
            </span>
          </div>
          <div className="form-row">
            <span className="form-label">mips</span>
            <span className="form-control">
              <NumberBox value={Number(res.mips ?? 1)} min={1} onCommit={(v) => setField('mips', v, `mips = ${v}`)} />
            </span>
          </div>
          <div className="form-row">
            <span className="form-label">samples</span>
            <span className="form-control">
              <NumberBox value={Number(res.samples ?? 1)} min={1} onCommit={(v) => setField('samples', v, `samples = ${v}`)} />
            </span>
          </div>
        </>
      )}
      {kind === 'buffer' && (
        <div className="form-row">
          <span className="form-label">bytes</span>
          <span className="form-control">
            <NumberBox value={Number(res.bytes ?? 0)} min={1} onCommit={(v) => setField('bytes', v, `bytes = ${v}`)} />
          </span>
        </div>
      )}
      {kind === 'texture' && (
        <div className="form-row">
          <span className="form-label">表示</span>
          <span className="form-control">
            <button onClick={() => showInViewer(name, String(res.format ?? ''))} title="リソースビューアーでビューポートに表示する">
              ビューアーで表示
            </button>
          </span>
        </div>
      )}
      <div className="form-row">
        <span className="form-label">寿命</span>
        <span className="form-control muted">{info?.used ? users.join(' → ') : '使われていません'}</span>
      </div>
    </div>
  );
}
