// ============================================================
//  インスペクター: ライトの物体の欄 (M5-5)
//
//  種類 (lights/*.light.json から)、種類の定義のパラメーター (color は色、float は数)、
//  照らす物体 (ライトリンク。どれも選ばなければすべての物体。親を選ぶと子も照らす)。
//  向きは物体の回転 (ローカルの -Z)。変えるとシーン全体の JSON を scene.set で送る (元に戻す / やり直すが効く)。
// ============================================================
import { useEffect } from 'react';
import type { LightParamInfo, LightTypeInfo, SceneJson, SceneObjectJson } from '../bridge/protocol';
import { lightParam, linkCandidates, loadLightTypes, withLightLinks, withLightParam, withLightType } from '../state/light';
import { fromHex, toHex } from '../state/scene';
import { useStudio } from '../state/store';
import { NumberBox } from './GraphEditors';

export function LightEditor({
  scene,
  obj,
  apply,
}: {
  scene: SceneJson;
  obj: SceneObjectJson;
  apply: (next: SceneJson, label: string) => Promise<void>;
}) {
  const types = useStudio((s) => s.lightTypes);
  useEffect(() => {
    if (types.length === 0) void loadLightTypes();
  }, [types.length]);
  const light = obj.light!;
  const type = types.find((t) => t.id === light.type);
  const links = light.links ?? [];
  const candidates = linkCandidates(scene);

  return (
    <>
      <div className="form-section muted">ライト (向きは回転のローカルの -Z)</div>
      <div className="form-row">
        <span className="form-label">種類</span>
        <span className="form-control">
          <select
            value={light.type}
            onChange={(e) => {
              const t = types.find((x) => x.id === e.target.value);
              if (t) void apply(withLightType(scene, obj.id, t), `種類 = ${t.label}`);
            }}
          >
            {!type && <option value={light.type}>{light.type} (ない)</option>}
            {types.map((t) => (
              <option key={t.id} value={t.id} disabled={!!t.error} title={t.error}>
                {t.label} ({t.id}){t.error ? ' — 使えない' : ''}
              </option>
            ))}
          </select>
        </span>
      </div>
      {type?.error && <div className="inspector-status error">{type.error}</div>}
      {type?.params.map((p) => (
        <LightParamRow key={p.name} p={p} values={lightParam(obj, type, p.name)} onCommit={(v, label) => void apply(withLightParam(scene, obj.id, p.name, v), label)} />
      ))}
      <div className="form-row">
        <span className="form-label" title="照らす物体を選ぶと、それ (と子) だけを照らす。選ばなければすべての物体">照らす物体</span>
        <span className="form-control light-links">
          <label className="light-link-all">
            <input type="checkbox" checked={links.length === 0} disabled={links.length === 0} onChange={() => void apply(withLightLinks(scene, obj.id, []), '照らす物体 = すべて')} />
            すべて
          </label>
          <div className="light-link-list">
            {candidates.map((c) => (
              <label key={c.id} title={c.id}>
                <input
                  type="checkbox"
                  checked={links.includes(c.id)}
                  onChange={(e) => {
                    const next = e.target.checked ? [...links, c.id] : links.filter((x) => x !== c.id);
                    void apply(withLightLinks(scene, obj.id, next), `照らす物体 = ${next.length ? next.join(', ') : 'すべて'}`);
                  }}
                />
                {c.name || c.id}
              </label>
            ))}
          </div>
        </span>
      </div>
    </>
  );
}

function LightParamRow({ p, values, onCommit }: { p: LightParamInfo; values: number[]; onCommit: (v: number[], label: string) => void }) {
  const label = p.label || p.name;
  return (
    <div className="form-row">
      <span className="form-label" title={`${p.name} (${p.type})`}>
        {label}
      </span>
      <span className="form-control material-values">
        {p.type === 'color' ? (
          <input type="color" value={toHex(values)} onChange={(e) => onCommit(fromHex(e.target.value).slice(0, 3), `${label} = ${e.target.value}`)} />
        ) : (
          values.map((x, i) => (
            <NumberBox
              key={i}
              value={Math.round(x * 10000) / 10000}
              min={p.min}
              step={0.1}
              onCommit={(n) => onCommit(values.map((y, k) => (k === i ? Math.min(Math.max(n, p.min ?? -Infinity), p.max ?? Infinity) : y)), `${label} = ${n}`)}
            />
          ))
        )}
      </span>
    </div>
  );
}

export function lightTypeLabel(types: LightTypeInfo[], id: string): string {
  return types.find((t) => t.id === id)?.label ?? id;
}
