// ============================================================
//  インスペクター: シーンの物体 (M5-2)
//
//  名前、モデル (立方体か、ライブラリのモデル、M5-3)、親、位置 / 回転 (度。X → Y → Z の順) / 大きさ、表示、
//  ライトの物体 (M5-5) はモデル / 色の代わりにライトの欄 (LightEditor)。
//  描画レイヤー (どの描画ノードで描くか。物体と、スロットごとの上書き。DrawLayerEditor)、色 (シェーダーの gColor)。
//  マテリアル (M5-4) の欄は 2026-10-22 に外した。
//  変えるとシーン全体の JSON を scene.set で送る (元に戻す / やり直すが効く。保存はシーンのパネル)。
// ============================================================
import { useEffect, useState } from 'react';
import type { SceneJson, SceneObjectJson } from '../bridge/protocol';
import { assetOfModel, loadLibrary, modelRef, objectSlots } from '../state/library';
import { applyScene, fromHex, parentCandidates, toHex, updateObject } from '../state/scene';
import { useStudio } from '../state/store';
import { NameBox, NumberBox } from './GraphEditors';
import { DrawLayerEditor } from './DrawLayerEditor';
import { LightEditor } from './LightEditor';

type Vec3 = [number, number, number];

export function ObjectEditor({ scene, id }: { scene: SceneJson; id: string }) {
  const [status, setStatus] = useState<{ kind: 'ok' | 'error'; text: string } | null>(null);
  const library = useStudio((s) => s.library);
  useEffect(() => {
    if (!library) void loadLibrary();
  }, [library]);
  const obj = scene.objects.find((o) => o.id === id);
  if (!obj) return <div className="placeholder">物体 '{id}' はありません</div>;

  const current = assetOfModel(library, obj.model);
  const slots = objectSlots(obj, library);
  const applyWhole = async (next: SceneJson, label: string) => {
    const err = await applyScene(next, `${obj.name}: ${label}`);
    setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: label });
  };
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
      {!obj.light && (
        <>
      <div className="form-row">
        <span className="form-label">モデル</span>
        <span className="form-control">
          <select value={obj.model} onChange={(e) => void set({ model: e.target.value }, `モデル = ${e.target.selectedOptions[0]?.text ?? e.target.value}`)}>
            <option value="builtin:cube">立方体 (builtin:cube)</option>
            {(library?.assets ?? [])
              .filter((a) => a.importer === 'model')
              .map((a) => (
                <option key={a.id} value={modelRef(a)}>
                  {a.path}
                </option>
              ))}
            {obj.model !== 'builtin:cube' && !current && <option value={obj.model}>{obj.model} (ライブラリにない)</option>}
          </select>
        </span>
      </div>
      {current?.model?.status === 'loading' && <div className="inspector-status busy">モデルを読み込んでいます…</div>}
      {current?.model?.status === 'error' && <div className="inspector-status error">{current.model.error}</div>}
      {current?.missing && <div className="inspector-status error">モデルの元のファイルがありません: {current.path}</div>}
        </>
      )}
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
      {obj.light ? (
        <LightEditor scene={scene} obj={obj} apply={applyWhole} />
      ) : (
        <>
      <div className="form-row">
        <span className="form-label" title="平行光源の影 (シャドウマップ) に描くか">影を落とす</span>
        <span className="form-control">
          <input type="checkbox" checked={obj.castShadow !== false} onChange={(e) => void set({ castShadow: e.target.checked ? undefined : false }, e.target.checked ? '影を落とす' : '影を落とさない')} />
        </span>
      </div>
      <div className="form-row">
        <span className="form-label" title="物体の色 (シェーダーの gColor。同じモデルを 1 体ずつ色違いにする)">色</span>
        <span className="form-control">
          <input type="color" value={toHex(obj.color)} onChange={(e) => void set({ color: fromHex(e.target.value, obj.color[3]) }, `色 = ${e.target.value}`)} />
        </span>
      </div>
      <DrawLayerEditor scene={scene} obj={obj} slots={slots} apply={applyWhole} />
        </>
      )}
    </div>
  );
}

// 表示用に丸める (0.1 + 0.2 のような誤差を出さない)
function round(v: number): number {
  return Math.round(v * 10000) / 10000;
}
