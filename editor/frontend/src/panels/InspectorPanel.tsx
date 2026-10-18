// ============================================================
//  インスペクター: グラフのパネルで選んだものを編集する
//
//    パス / リソース → GraphEditors (グラフの JSON を graph.set で送る)
//    パイプライン    → パイプラインのアセットの state (GfxGraphicsPipelineDesc) を、構造体のメンバー単位で編集する
//      pipeline.list → 選んだアセットの pipeline.get と、その型の schema.describe → SchemaForm
//      値を変えると pipeline.update を送り、エンジンが PSO を作り直す (ビューポートにすぐ反映される)。
//      不正な値や PSO を作れない組み合わせはエラーになり、エンジン側は元の設定のまま (理由はログ)。
//      「保存」でアセットのファイル (pipelines/*.json) に書く。
//      RTV / DSV のフォーマットはパスの出力から決まるので、ここで変えても使われない。
// ============================================================
import { useCallback, useEffect, useRef, useState } from 'react';
import { call, isConnected, on, StudioError } from '../bridge/client';
import type { JsonObject, JsonValue, PipelineInfo, TypeDescription } from '../bridge/protocol';
import { PassEditor, ResourceEditor } from '../inspector/GraphEditors';
import { ObjectEditor } from '../inspector/ObjectEditor';
import { SchemaForm, formatPath, type Path } from '../inspector/SchemaForm';
import { studioStore, useStudio } from '../state/store';

// path の位置を value に置き換えた写しを返す
function setIn(obj: JsonValue, path: Path, value: JsonValue): JsonValue {
  if (path.length === 0) return value;
  const [head, ...rest] = path;
  if (typeof head === 'number') {
    const arr = Array.isArray(obj) ? obj.slice() : [];
    arr[head] = setIn(arr[head] ?? null, rest, value);
    return arr;
  }
  const o = obj && typeof obj === 'object' && !Array.isArray(obj) ? { ...obj } : {};
  o[head] = setIn(o[head] ?? null, rest, value);
  return o;
}

const schemaCache = new Map<string, TypeDescription>();

type Status = { kind: 'idle' | 'busy' | 'ok' | 'error'; text: string };

export function InspectorPanel() {
  const selection = useStudio((s) => s.selection);
  const graph = useStudio((s) => s.graph);
  const scene = useStudio((s) => s.scene);
  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;

  if (selection?.kind === 'object' && scene) {
    const obj = scene.scene.objects.find((o) => o.id === selection.id);
    return (
      <div className="panel inspector">
        <header className="panel-header">
          <span className="panel-title">物体: {obj?.name || selection.id}</span>
          <span className="spacer" />
          <button onClick={() => studioStore.select(null)} title="パイプラインの編集に戻る">
            パイプライン
          </button>
        </header>
        <div className="inspector-body">
          <ObjectEditor scene={scene.scene} id={selection.id} />
        </div>
      </div>
    );
  }
  if ((selection?.kind === 'pass' || selection?.kind === 'resource') && graph) {
    return (
      <div className="panel inspector">
        <header className="panel-header">
          <span className="panel-title">
            {selection.kind === 'pass' ? 'パス' : 'リソース'}: {selection.name}
          </span>
          <span className="spacer" />
          <button onClick={() => studioStore.select(null)} title="パイプラインの編集に戻る">
            パイプライン
          </button>
        </header>
        <div className="inspector-body">
          {selection.kind === 'pass' ? <PassEditor graph={graph} name={selection.name} /> : <ResourceEditor graph={graph} name={selection.name} />}
        </div>
      </div>
    );
  }
  return <PipelineInspector want={selection?.kind === 'pipeline' ? selection.id : ''} />;
}

function PipelineInspector({ want }: { want: string }) {
  const engine = useStudio((s) => s.engine);
  const [pipelines, setPipelines] = useState<PipelineInfo[]>([]);
  const [selected, setSelected] = useState<string>('');
  const [schema, setSchema] = useState<TypeDescription | null>(null);
  const [desc, setDesc] = useState<JsonObject | null>(null);
  const [filter, setFilter] = useState('');
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });

  // 送信中にさらに変更されたら、最後の値だけを送る
  const sending = useRef(false);
  const pending = useRef<JsonObject | null>(null);
  const label = useRef('');   // 履歴に出す名前 (最後に変えたフィールド)

  const errorText = (e: unknown) => (e instanceof StudioError ? e.message : String(e));

  // パイプラインの一覧 (エンジンの準備ができてから)
  useEffect(() => {
    if (!isConnected() || engine?.status !== 'ready') return;
    call('pipeline.list')
      .then((list) => {
        setPipelines(list);
        setSelected((cur) => (want && list.some((p) => p.id === want) ? want : cur && list.some((p) => p.id === cur) ? cur : (list[0]?.id ?? '')));
      })
      .catch((e) => setStatus({ kind: 'error', text: errorText(e) }));
  }, [engine?.status, want]);

  const load = useCallback(async (id: string) => {
    if (!id) return;
    try {
      const got = await call('pipeline.get', { id });
      let s = schemaCache.get(got.type);
      if (!s) {
        s = await call('schema.describe', { type: got.type });
        schemaCache.set(got.type, s);
      }
      setSchema(s);
      setDesc(got.desc);
      setStatus({ kind: 'idle', text: '' });
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  }, []);

  useEffect(() => {
    void load(selected);
  }, [selected, load]);

  // 元に戻す / やり直すで、表示しているパイプラインが変わったら読み直す
  useEffect(
    () =>
      on('pipeline.changed', (e) => {
        if (e.id === selected) void load(selected);
      }),
    [selected, load],
  );

  const send = useCallback(
    async (next: JsonObject) => {
      if (sending.current) {
        pending.current = next;
        return;
      }
      sending.current = true;
      setStatus({ kind: 'busy', text: '反映しています…' });
      try {
        const res = await call('pipeline.update', { id: selected, desc: next, label: label.current || `${selected} を編集` });
        setDesc(res.desc);
        setStatus({
          kind: 'ok',
          text: res.warnings.length ? `反映しました (警告: ${res.warnings.join(' / ')})` : '反映しました',
        });
      } catch (e) {
        setStatus({ kind: 'error', text: errorText(e) });
      } finally {
        sending.current = false;
        const p = pending.current;
        pending.current = null;
        if (p) void send(p);
      }
    },
    [selected],
  );

  const onChange = (path: Path, value: JsonValue) => {
    if (!desc) return;
    const next = setIn(desc, path, value) as JsonObject;
    setDesc(next);
    setStatus({ kind: 'busy', text: `${formatPath(path)} を変更` });
    label.current = `${selected}: ${formatPath(path)} = ${JSON.stringify(value)}`;
    void send(next);
  };

  const save = async () => {
    setStatus({ kind: 'busy', text: '保存しています…' });
    try {
      await call('pipeline.save', { id: selected });
      setStatus({ kind: 'ok', text: `保存しました (${selected})` });
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  };

  return (
    <div className="panel inspector">
      <header className="panel-header">
        <select
          value={selected}
          onChange={(e) => {
            setSelected(e.target.value);
            studioStore.select({ kind: 'pipeline', id: e.target.value });
          }}
          disabled={pipelines.length === 0}
        >
          {pipelines.map((p) => (
            <option key={p.id} value={p.id}>
              {p.label}
            </option>
          ))}
        </select>
        <input className="inspector-filter" placeholder="フィールドを絞り込み" value={filter} onChange={(e) => setFilter(e.target.value)} />
        <button onClick={() => load(selected)} title="エンジンの今の値を読み直す (編集を取り消すときはツールバーの「元に戻す」)">
          読み直す
        </button>
        <button onClick={() => void save()} disabled={!selected} title="パイプラインのアセットに書く">
          保存
        </button>
      </header>
      {status.text && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}
      <div className="inspector-body">
        {schema && desc ? (
          <SchemaForm schema={schema} value={desc} filter={filter} onChange={onChange} />
        ) : (
          <div className="placeholder">{engine?.status === 'ready' ? '読み込んでいます…' : 'エンジンの準備を待っています…'}</div>
        )}
      </div>
    </div>
  );
}
