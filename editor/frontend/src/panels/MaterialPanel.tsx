// ============================================================
//  マテリアルのタブ (M5-4): materials/*.material.json を編集する
//
//  欄はマテリアルのシェーダーのリフレクション (cbuffer Material の変数、テクスチャの変数) から作る。
//    マテリアルパス  パスごとのパイプライン (pipelines/ の JSON から選ぶ。「シェーダーを開く」でパイプラインのタブ)
//    パラメーター    float / int / uint は数、bool はチェック、名前に Color を含む float3 / float4 は色
//    テクスチャ      ライブラリのテクスチャから選ぶ (なしなら既定: 白 / 平らな法線)
//  変えるとすぐファイルに書き、描画に反映する (material.set。元に戻す / やり直すが効く)。
//  タブの id は "material:<パス>"。ファイルが外で変わったら (material.changed) 読み直す。
// ============================================================
import { useCallback, useEffect, useRef, useState } from 'react';
import type { IDockviewPanelProps } from 'dockview-react';
import { call, isConnected, on } from '../bridge/client';
import type { MaterialInfo, MaterialJson, MaterialPassInfo, MaterialVar } from '../bridge/protocol';
import { NumberBox } from '../inspector/GraphEditors';
import { fileName } from '../state/assets';
import { openPipelineTab } from '../state/dock';
import { errorText } from '../state/graph';
import { loadLibrary } from '../state/library';
import { isColorParam, paramValue, paramValues, withParam, withPipeline, withTexture } from '../state/material';
import { fromHex, toHex } from '../state/scene';
import { useStudio } from '../state/store';

export interface MaterialParams {
  path: string;
}

type Status = { kind: 'idle' | 'ok' | 'error'; text: string };

export function MaterialPanel(props: IDockviewPanelProps<MaterialParams>) {
  const { path } = props.params;
  const [info, setInfo] = useState<MaterialInfo | null>(null);
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });
  const [pipelines, setPipelines] = useState<string[]>([]);
  const library = useStudio((s) => s.library);
  const busy = useRef(false);

  const reload = useCallback(async () => {
    try {
      setInfo(await call('material.describe', { key: path }));
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  }, [path]);

  useEffect(() => {
    if (!isConnected()) return;
    void reload();
    if (!library) void loadLibrary();
    call('asset.list', { dir: 'pipelines', extensions: ['.json'] })
      .then((r) => setPipelines(r.files))
      .catch(() => setPipelines([]));
    // 描画スレッドがファイルを読み直すのを待ってから、説明を取り直す
    return on('material.changed', (e) => {
      if (e.path === path && !busy.current) window.setTimeout(() => void reload(), 150);
    });
  }, [path, reload, library]);

  useEffect(() => {
    props.api.setTitle(`マテリアル: ${fileName(path)}`);
  }, [path, props.api]);

  const apply = async (next: MaterialJson, label: string) => {
    busy.current = true;
    try {
      await call('material.set', { path, material: next, label: `${fileName(path)}: ${label}` });
      setStatus({ kind: 'ok', text: label });
      await new Promise((r) => setTimeout(r, 120));
      await reload();
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    } finally {
      busy.current = false;
    }
  };

  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;
  if (!info) return <div className="panel"><div className="placeholder">マテリアルを読み込んでいます…</div></div>;
  const m = info.material;
  const textures = (library?.assets ?? []).filter((a) => a.importer === 'texture');

  return (
    <div className="panel material-panel">
      <header className="panel-header">
        <span className="panel-title" title={path}>{path}</span>
        <span className="spacer" />
        <button onClick={() => void call('asset.openExternal', { path, app: 'vscode' }).catch(() => {})} title="JSON を VS Code で開く">
          VS Code
        </button>
      </header>
      {info.error && <div className="inspector-status error">{info.error}</div>}
      {status.text && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}
      {m && info.passes && (
        <div className="graph-editor material-editor">
          {Object.entries(info.passes).map(([pass, p]) => (
            <PassSection
              key={pass}
              pass={pass}
              info={p}
              material={m}
              pipelines={pipelines}
              textures={textures.map((t) => ({ value: `asset:${t.id}`, label: t.path }))}
              apply={apply}
            />
          ))}
          {Object.keys(info.passes).length === 0 && <div className="placeholder">マテリアルパスがありません (passes に足すと、そのパスで描かれます)</div>}
          <UnusedParams material={m} passes={info.passes} />
        </div>
      )}
    </div>
  );
}

function PassSection({
  pass,
  info,
  material,
  pipelines,
  textures,
  apply,
}: {
  pass: string;
  info: MaterialPassInfo;
  material: MaterialJson;
  pipelines: string[];
  textures: { value: string; label: string }[];
  apply: (next: MaterialJson, label: string) => Promise<void>;
}) {
  return (
    <section className="material-pass">
      <div className="form-section">マテリアルパス: {pass}</div>
      <div className="form-row">
        <span className="form-label">パイプライン</span>
        <span className="form-control">
          <select value={info.pipeline} onChange={(e) => void apply(withPipeline(material, pass, e.target.value), `${pass} のパイプライン = ${e.target.value}`)}>
            {!pipelines.includes(info.pipeline) && <option value={info.pipeline}>{info.pipeline}</option>}
            {pipelines.map((p) => (
              <option key={p} value={p}>
                {p}
              </option>
            ))}
          </select>
          <button onClick={() => openPipelineTab(info.pipeline)} title="パイプラインのタブ (シェーダーを開く)">
            開く
          </button>
        </span>
      </div>
      {!info.ok && <div className="inspector-status error">{info.error}</div>}
      {info.cbuffer.length > 0 && <div className="form-section muted">パラメーター (cbuffer Material)</div>}
      {info.cbuffer.map((v) => (
        <ParamRow key={v.name} v={v} material={material} apply={apply} />
      ))}
      {info.textures.length > 0 && <div className="form-section muted">テクスチャ</div>}
      {info.textures.map((t) => (
        <div className="form-row" key={t.name}>
          <span className="form-label" title={`t${t.slot}、${t.srgb ? 'sRGB' : 'リニア'}`}>
            {t.name}
          </span>
          <span className="form-control">
            <select
              value={material.textures[t.name] ?? ''}
              onChange={(e) => void apply(withTexture(material, t.name, e.target.value || null), `${t.name} = ${e.target.selectedOptions[0]?.text}`)}
            >
              <option value="">(なし: {t.default === 'flatNormal' ? '平らな法線' : '白'})</option>
              {material.textures[t.name] && !textures.some((x) => x.value === material.textures[t.name]) && (
                <option value={material.textures[t.name]}>{material.textures[t.name]} (ライブラリにない)</option>
              )}
              {textures.map((x) => (
                <option key={x.value} value={x.value}>
                  {x.label}
                </option>
              ))}
            </select>
          </span>
        </div>
      ))}
    </section>
  );
}

function ParamRow({ v, material, apply }: { v: MaterialVar; material: MaterialJson; apply: (next: MaterialJson, label: string) => Promise<void> }) {
  const values = paramValues(material, v);
  const commit = (next: number[], label: string) => void apply(withParam(material, v.name, paramValue(v, next)), label);
  const typeLabel = `${v.type}${v.columns > 1 ? v.columns : ''}${v.elements ? `[${v.elements}]` : ''}`;
  let control: React.ReactNode;
  if (v.rows !== 1) control = <span className="muted">行列はまだ編集できません</span>;
  else if (v.type === 'bool' && values.length === 1)
    control = <input type="checkbox" checked={values[0] !== 0} onChange={(e) => commit([e.target.checked ? 1 : 0], `${v.name} = ${e.target.checked}`)} />;
  else if (isColorParam(v))
    control = (
      <span className="material-color">
        <ColorField value={toHex(values)} onCommit={(hex) => {
          const c = fromHex(hex, values[3] ?? 1);
          commit(v.columns === 4 ? c : c.slice(0, 3), `${v.name} = ${hex}`);
        }} />
        {values.map((x, i) => (
          <NumberBox key={i} value={round(x)} step={0.05} onCommit={(n) => commit(values.map((y, k) => (k === i ? n : y)), `${v.name}[${i}] = ${n}`)} />
        ))}
      </span>
    );
  else
    control = (
      <span className="material-values">
        {values.map((x, i) => (
          <NumberBox key={i} value={round(x)} step={v.type === 'float' ? 0.05 : 1} onCommit={(n) => commit(values.map((y, k) => (k === i ? n : y)), `${v.name}${values.length > 1 ? `[${i}]` : ''} = ${n}`)} />
        ))}
      </span>
    );
  return (
    <div className="form-row">
      <span className="form-label" title={`${typeLabel}、オフセット ${v.offset}`}>
        {v.name}
      </span>
      <span className="form-control">{control}</span>
    </div>
  );
}

// 色の選択は、ダイアログを閉じたとき (change) だけ書く (動かしているあいだに何度も書かない)
function ColorField({ value, onCommit }: { value: string; onCommit: (hex: string) => void }) {
  const ref = useRef<HTMLInputElement>(null);
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    el.value = value;
    const onChange = () => onCommit(el.value);
    el.addEventListener('change', onChange);
    return () => el.removeEventListener('change', onChange);
  }, [value, onCommit]);
  return <input ref={ref} type="color" defaultValue={value} />;
}

// シェーダーにない値 (シェーダーを替えたあとなど)。ファイルには残る
function UnusedParams({ material, passes }: { material: MaterialJson; passes: Record<string, MaterialPassInfo> }) {
  const known = new Set(Object.values(passes).flatMap((p) => [...p.cbuffer.map((v) => v.name), ...p.textures.map((t) => t.name)]));
  const unused = [...Object.keys(material.params), ...Object.keys(material.textures)].filter((k) => !known.has(k));
  if (unused.length === 0) return null;
  return <div className="muted material-unused">シェーダーにない値 (ファイルには残っています): {unused.join(', ')}</div>;
}

function round(v: number): number {
  return Math.round(v * 10000) / 10000;
}
