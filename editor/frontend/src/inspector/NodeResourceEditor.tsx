// ============================================================
//  インスペクター: 物体の描画ノードのリソース (2026-10-22)
//
//  物体のメッシュを描く描画ノード (描画レイヤー、影を落とすなら Shadow) ごとに、そのパイプラインのシェーダーが使う
//  cbuffer の変数とテクスチャ (グラフがつながないもの) の欄を出す (scene.nodeResources。C++ がリフレクションで調べる)。
//  「設定する対象」で物体全体かスロットを選ぶ。値はシーンの nodeParams / slotNodeParams に書く (元に戻せる)。
//  設定していない欄は、取り込んだ値 (モデルのスロット。名前で合わせる) か既定値を薄く出す。× で設定を外す。
// ============================================================
import { useEffect, useState } from 'react';
import { call, isConnected } from '../bridge/client';
import type { JsonValue, NodeResourceInfo, NodeResourceVar, SceneJson, SceneObjectJson } from '../bridge/protocol';
import { errorText } from '../state/graph';
import { assetName } from '../state/library';
import { defaultNumbers, effectiveValue, type ParamTarget, toNumbers, withNodeParam } from '../state/nodeParams';
import { fromHex, toHex } from '../state/scene';
import { useStudio } from '../state/store';
import { NumberBox } from './GraphEditors';

interface Props {
  scene: SceneJson;
  obj: SceneObjectJson;
  slots: string[];
  apply: (next: SceneJson, label: string) => Promise<void>;
}

const SOURCE_LABEL: Record<string, string> = { slot: 'スロット', object: '物体', imported: '取り込んだ値', default: '既定' };

export function NodeResourceEditor({ scene, obj, slots, apply }: Props) {
  const graphRevision = useStudio((s) => s.graph?.revision ?? 0);
  const library = useStudio((s) => s.library);
  const shaderErrors = useStudio((s) => s.shaderErrors);
  const [nodes, setNodes] = useState<NodeResourceInfo[] | null>(null);
  const [error, setError] = useState('');
  const [target, setTarget] = useState<ParamTarget>({});
  // 描画レイヤー / モデル / グラフ / シェーダーが変わったら調べ直す (値はシーンの JSON から読むので、値の変更では呼ばない)
  const key = JSON.stringify([obj.id, obj.model, obj.layers, obj.slotLayers, obj.castShadow, graphRevision, Object.keys(shaderErrors).length, library?.assets.length]);
  useEffect(() => {
    if (!isConnected()) return;
    let alive = true;
    // モデルの読み込みや、描画スレッドが物体を作り直すのを待ってから
    const t = window.setTimeout(() => {
      call('scene.nodeResources', { id: obj.id })
        .then((r) => alive && (setNodes(r.nodes), setError('')))
        .catch((e) => alive && setError(errorText(e)));
    }, 150);
    return () => {
      alive = false;
      window.clearTimeout(t);
    };
  }, [key]);
  useEffect(() => {
    if (target.slot !== undefined && !slots.includes(target.slot)) setTarget({});
  }, [slots, target.slot]);

  if (error) return <div className="inspector-status error">{error}</div>;
  if (!nodes || nodes.length === 0) return null;
  const textures = (library?.assets ?? []).filter((a) => a.importer === 'texture');
  const targetLabel = target.slot === undefined ? '物体' : target.slot;
  const set = (layer: string, name: string, value: JsonValue | undefined, label: string) =>
    void apply(withNodeParam(scene, obj.id, target, layer, name, value), `${targetLabel} の ${layer}.${name}: ${label}`);

  return (
    <>
      <div className="form-section muted" title="描画ノードのシェーダーが使う定数とテクスチャ (グラフがつながないもの)。物体ごとに設定する">
        描画ノードのリソース
      </div>
      {slots.length > 1 && (
        <div className="form-row">
          <span className="form-label" title="物体全体に設定するか、スロットだけ上書きするか">設定する対象</span>
          <span className="form-control">
            <select value={target.slot ?? ''} onChange={(e) => setTarget(e.target.value ? { slot: e.target.value } : {})}>
              <option value="">物体全体</option>
              {slots.map((s) => (
                <option key={s} value={s}>
                  スロット: {s}
                </option>
              ))}
            </select>
          </span>
        </div>
      )}
      {nodes.map((n) => {
        const imported = (n.imported ?? {}) as Record<string, Record<string, JsonValue>>;
        const vars = n.cbuffers.flatMap((c) => c.vars.map((v) => ({ cb: c.name, v })));
        return (
          <div key={n.layer} className="node-resources">
            <div className="node-resources-title" title={`${n.pipeline} (layer: ${n.layer})`}>
              {n.pass} <span className="muted">{n.pipeline.replace(/^pipelines\//, '')}</span>
            </div>
            {!n.ok && <div className="inspector-status error">{n.error}</div>}
            {n.ok && vars.length === 0 && n.textures.length === 0 && <div className="muted object-slots-note">物体ごとのリソースはありません</div>}
            {vars.map(({ cb, v }) => {
              const eff = effectiveValue(obj, n.layer, target, v.name, imported);
              return (
                <div className="form-row" key={`${cb}.${v.name}`}>
                  <span className={`form-label ${eff.source === 'slot' || eff.source === 'object' ? 'changed' : ''}`} title={`cbuffer ${cb} の ${v.type}${v.columns > 1 ? v.columns : ''}${v.elements ? `[${v.elements}]` : ''} (${SOURCE_LABEL[eff.source]})`}>
                    {v.name}
                  </span>
                  <span className="form-control node-resource-value">
                    <VarField v={v} value={eff.value} source={eff.source} onSet={(val, label) => set(n.layer, v.name, val, label)} />
                    {(eff.source === 'slot' && target.slot !== undefined) || (eff.source === 'object' && target.slot === undefined) ? (
                      <button className="reset" title="設定を外す (取り込んだ値 / 既定に戻す)" onClick={() => set(n.layer, v.name, undefined, '設定を外す')}>
                        ×
                      </button>
                    ) : null}
                  </span>
                </div>
              );
            })}
            {n.textures.map((t) => {
              const eff = effectiveValue(obj, n.layer, target, t.name, imported);
              const current = typeof eff.value === 'string' ? eff.value : '';
              const own = (eff.source === 'slot' && target.slot !== undefined) || (eff.source === 'object' && target.slot === undefined);
              const defaultLabel = t.default === 'flatNormal' ? '平らな法線' : '白';
              return (
                <div className="form-row" key={`tex.${t.name}`}>
                  <span className={`form-label ${own ? 'changed' : ''}`} title={`Texture2D (${t.srgb ? 'sRGB' : 'リニア'}。${SOURCE_LABEL[eff.source]})`}>
                    {t.name}
                  </span>
                  <span className="form-control node-resource-value">
                    <select
                      value={own ? current : ''}
                      onChange={(e) => set(n.layer, t.name, e.target.value || undefined, e.target.value ? assetLabel(e.target.value) : '設定を外す')}
                    >
                      <option value="">
                        {eff.source === 'imported' || (!own && current) ? `(${SOURCE_LABEL[eff.source]}: ${assetLabel(current)})` : `(既定: ${defaultLabel})`}
                      </option>
                      {textures.map((a) => (
                        <option key={a.id} value={`asset:${a.id}`}>
                          {assetName(a.path)}
                        </option>
                      ))}
                    </select>
                    {own && (
                      <button className="reset" title="設定を外す (取り込んだ値 / 既定に戻す)" onClick={() => set(n.layer, t.name, undefined, '設定を外す')}>
                        ×
                      </button>
                    )}
                  </span>
                </div>
              );
            })}
          </div>
        );
      })}
    </>
  );

  function assetLabel(ref: string): string {
    const id = ref.startsWith('asset:') ? ref.slice(6) : ref;
    const a = (library?.assets ?? []).find((x) => x.id === id);
    return a ? assetName(a.path) : ref;
  }
}

// 1 つの変数の欄 (float1〜4 は数、名前に Color を含む float3 / float4 は色、bool はチェック)
function VarField({ v, value, source, onSet }: { v: NodeResourceVar; value: JsonValue | undefined; source: string; onSet: (v: JsonValue, label: string) => void }) {
  const count = v.columns * Math.max(v.elements, 1);
  if (v.rows !== 1) return <span className="muted">行列はまだ設定できません</span>;
  const nums = toNumbers(value, count) ?? defaultNumbers(v.name, v.type, count);
  const faint = source === 'imported' || source === 'default';
  if (v.type === 'bool' && count === 1)
    return <input type="checkbox" className={faint ? 'faint' : ''} checked={nums[0] !== 0} onChange={(e) => onSet(e.target.checked, String(e.target.checked))} />;
  const isColor = v.type === 'float' && (v.columns === 3 || v.columns === 4) && !v.elements && v.name.toLowerCase().includes('color');
  const pack = (next: number[]): JsonValue => (count === 1 ? next[0] : next);
  return (
    <span className={`node-resource-numbers ${faint ? 'faint' : ''}`}>
      {isColor && (
        <input
          type="color"
          value={toHex(nums)}
          onChange={(e) => {
            const c = fromHex(e.target.value, nums[3] ?? 1);
            onSet(pack(v.columns === 3 ? c.slice(0, 3) : c), e.target.value);
          }}
        />
      )}
      {nums.map((x, i) => (
        <NumberBox
          key={i}
          value={Math.round(x * 1e4) / 1e4}
          step={v.type === 'float' ? 0.05 : 1}
          onCommit={(nv) => {
            const next = [...nums];
            next[i] = v.type === 'float' ? nv : Math.round(nv);
            onSet(pack(next), String(next.map((y) => Math.round(y * 1e4) / 1e4)));
          }}
        />
      ))}
    </span>
  );
}
