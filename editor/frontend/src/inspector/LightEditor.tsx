// ============================================================
//  インスペクター: ライトの物体の欄 (M5-5)
//
//  種類 (lights/*.light.json から)、種類の定義のパラメーター (color は色、float は数)、
//  照らす物体 (ライトリンク。どれも選ばなければすべての物体。親を選ぶと子も照らす)、
//  影 (平行光源だけ。影を落とすか、bias / normalBias / ぼかし。今は影を落とす設定の最初の平行光源 1 つだけが使う)。
//  向きは物体の回転 (ローカルの -Z)。変えるとシーン全体の JSON を scene.set で送る (元に戻す / やり直すが効く)。
// ============================================================
import { useEffect } from 'react';
import type { LightParamInfo, LightTypeInfo, SceneJson, SceneObjectJson } from '../bridge/protocol';
import { lightParam, lightShadow, linkCandidates, loadLightTypes, withLightLinks, withLightParam, withLightShadow, withLightType } from '../state/light';
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
      {type?.shape === 'directional' && <ShadowRows scene={scene} obj={obj} apply={apply} />}
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

function ShadowRows({ scene, obj, apply }: { scene: SceneJson; obj: SceneObjectJson; apply: (next: SceneJson, label: string) => Promise<void> }) {
  const sh = lightShadow(obj);
  const num = (key: 'bias' | 'normalBias' | 'softness', label: string, step: number, title: string) => (
    <div className="form-row">
      <span className="form-label" title={title}>
        {label}
      </span>
      <span className="form-control">
        <NumberBox value={sh[key]} min={0} step={step} onCommit={(v) => void apply(withLightShadow(scene, obj.id, { [key]: Math.max(0, v) }), `${label} = ${v}`)} />
      </span>
    </div>
  );
  return (
    <>
      <div className="form-row">
        <span className="form-label" title="影を落とす (シャドウマップ。今は、影を落とす設定の最初の平行光源だけ)">影を落とす</span>
        <span className="form-control">
          <input type="checkbox" checked={sh.enabled} onChange={(e) => void apply(withLightShadow(scene, obj.id, { enabled: e.target.checked }), e.target.checked ? '影を落とす' : '影を落とさない')} />
        </span>
      </div>
      {sh.enabled && (
        <>
          {num('bias', '影の bias', 0.0005, '深さの比較のずれ。大きいと影が浮き、小さいと面に縞 (シャドウアクネ) が出る')}
          {num('normalBias', '影の normalBias (m)', 0.01, '法線の向きに押し出す量。斜めの面の縞を消す')}
          {num('softness', '影のぼかし (テクセル)', 0.5, '縁をぼかす幅 (シャドウマップのテクセル)')}
        </>
      )}
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
