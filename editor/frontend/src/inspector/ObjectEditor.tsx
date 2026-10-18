// ============================================================
//  インスペクター: シーンの物体 (M5-2)
//
//  名前、親、位置 / 回転 (度。X → Y → Z の順) / 大きさ、表示、色と layer (マテリアル (M5-4) までの仮の項目)。
//  変えるとシーン全体の JSON を scene.set で送る (元に戻す / やり直すが効く。保存はシーンのパネル)。
// ============================================================
import { useState } from 'react';
import type { SceneJson, SceneObjectJson } from '../bridge/protocol';
import { applyScene, fromHex, parentCandidates, toHex, updateObject } from '../state/scene';
import { NameBox, NumberBox } from './GraphEditors';

type Vec3 = [number, number, number];

export function ObjectEditor({ scene, id }: { scene: SceneJson; id: string }) {
  const [status, setStatus] = useState<{ kind: 'ok' | 'error'; text: string } | null>(null);
  const obj = scene.objects.find((o) => o.id === id);
  if (!obj) return <div className="placeholder">物体 '{id}' はありません</div>;

  const set = async (patch: Partial<SceneObjectJson>, label: string) => {
    const err = await applyScene(updateObject(scene, id, patch), `${obj.name}: ${label}`);
    setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: label });
  };
  const vec = (key: 'position' | 'rotation' | 'scale', label: string, step: number) => (
    <div className="form-row">
      <span className="form-label">{label}</span>
      <span className="form-control vec3">
        {(['X', 'Y', 'Z'] as const).map((axis, i) => (
          <label key={axis} className={`vec3-${axis.toLowerCase()}`}>
            {axis}
            <NumberBox
              value={round(obj[key][i])}
              step={step}
              onCommit={(v) => {
                const next = [...obj[key]] as Vec3;
                next[i] = v;
                void set({ [key]: next }, `${label} ${axis} = ${v}`);
              }}
            />
          </label>
        ))}
      </span>
    </div>
  );

  return (
    <div className="graph-editor object-editor">
      {status && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}
      <div className="form-row">
        <span className="form-label">名前</span>
        <span className="form-control">
          <NameBox value={obj.name} onCommit={(v) => void set({ name: v }, `名前 = ${v}`)} />
        </span>
      </div>
      <div className="form-row">
        <span className="form-label">id</span>
        <span className="form-control muted">{obj.id}</span>
      </div>
      <div className="form-row">
        <span className="form-label">モデル</span>
        <span className="form-control muted" title="モデルの読み込みは M5-3">{obj.model}</span>
      </div>
      <div className="form-row">
        <span className="form-label">親</span>
        <span className="form-control">
          <select value={obj.parent ?? ''} onChange={(e) => void set({ parent: e.target.value || null }, `親 = ${e.target.value || 'なし'}`)}>
            <option value="">(なし)</option>
            {parentCandidates(scene, id).map((p) => (
              <option key={p.id} value={p.id}>
                {p.name || p.id}
              </option>
            ))}
          </select>
        </span>
      </div>
      {vec('position', '位置', 0.1)}
      {vec('rotation', '回転 (度)', 5)}
      {vec('scale', '大きさ', 0.1)}
      <div className="form-row">
        <span className="form-label">表示</span>
        <span className="form-control">
          <input type="checkbox" checked={obj.visible} onChange={(e) => void set({ visible: e.target.checked }, e.target.checked ? '表示' : '非表示')} />
        </span>
      </div>
      <div className="form-section muted">仮の項目 (マテリアルができるまで)</div>
      <div className="form-row">
        <span className="form-label">色</span>
        <span className="form-control">
          <input type="color" value={toHex(obj.color)} onChange={(e) => void set({ color: fromHex(e.target.value, obj.color[3]) }, `色 = ${e.target.value}`)} />
        </span>
      </div>
      <div className="form-row">
        <span className="form-label">layer</span>
        <span className="form-control">
          <NameBox value={obj.layer} onCommit={(v) => void set({ layer: v }, `layer = ${v}`)} />
        </span>
      </div>
    </div>
  );
}

// 表示用に丸める (0.1 + 0.2 のような誤差を出さない)
function round(v: number): number {
  return Math.round(v * 10000) / 10000;
}
