// ============================================================
//  インスペクター: シーンの物体 (M5-2)
//
//  名前、モデル (立方体か、ライブラリのモデル、M5-3)、親、位置 / 回転 (度。X → Y → Z の順) / 大きさ、表示、
//  色 (マテリアルの色に掛ける)、スロットごとのマテリアル (M5-4。取り込んだ値 / materials/ のファイル。
//  「マテリアルにする」で取り込んだ値をファイルに書き出して割り当てる。「開く」でマテリアルのタブ)。
//  変えるとシーン全体の JSON を scene.set で送る (元に戻す / やり直すが効く。保存はシーンのパネル)。
// ============================================================
import { useEffect, useState } from 'react';
import type { SceneJson, SceneObjectJson } from '../bridge/protocol';
import { call } from '../bridge/client';
import { openMaterialTab } from '../state/dock';
import { errorText } from '../state/graph';
import { assetName, assetOfModel, loadLibrary, modelRef } from '../state/library';
import { autoMaterialKey, loadMaterialFiles, newMaterialPath, objectSlots, withAssignment } from '../state/material';
import { applyScene, fromHex, parentCandidates, toHex, updateObject } from '../state/scene';
import { useStudio } from '../state/store';
import { NameBox, NumberBox } from './GraphEditors';

type Vec3 = [number, number, number];

export function ObjectEditor({ scene, id }: { scene: SceneJson; id: string }) {
  const [status, setStatus] = useState<{ kind: 'ok' | 'error'; text: string } | null>(null);
  const library = useStudio((s) => s.library);
  const materialFiles = useStudio((s) => s.materialFiles);
  useEffect(() => {
    if (!library) void loadLibrary();
  }, [library]);
  const obj = scene.objects.find((o) => o.id === id);
  if (!obj) return <div className="placeholder">物体 '{id}' はありません</div>;

  const current = assetOfModel(library, obj.model);
  const slots = objectSlots(obj, library);
  const assign = async (slot: string, path: string | null) => {
    const err = await applyScene(withAssignment(scene, id, slot, path), `${obj.name}: ${slot} のマテリアル = ${path ?? '取り込んだ値'}`);
    setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: `${slot} = ${path ?? '取り込んだ値'}` });
  };
  // 取り込んだ値をファイルに書き出して割り当てる
  const makeMaterial = async (slot: string, index: number) => {
    try {
      const auto = await call('material.describe', { key: autoMaterialKey(obj, index) });
      if (!auto.material) throw new Error(auto.error ?? '取り込んだ値のマテリアルを読めません');
      const base = current ? assetName(current.path) : obj.name || obj.id;
      const path = newMaterialPath(base, slot, await loadMaterialFiles());
      await call('material.create', { path, material: auto.material });
      await loadMaterialFiles();
      await assign(slot, path);
      openMaterialTab(path);
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
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
      <div className="form-row">
        <span className="form-label" title="マテリアルの色に掛ける色 (同じモデルを 1 体ずつ色違いにする)">色 (掛ける)</span>
        <span className="form-control">
          <input type="color" value={toHex(obj.color)} onChange={(e) => void set({ color: fromHex(e.target.value, obj.color[3]) }, `色 = ${e.target.value}`)} />
        </span>
      </div>
      <div className="form-section muted">マテリアル (スロットごと)</div>
      {slots.length === 0 && <div className="muted object-slots-note">モデルを読み込むと、スロットが出ます</div>}
      {slots.map((slot, i) => {
        const assigned = obj.materials?.[slot] ?? '';
        return (
          <div className="form-row" key={slot}>
            <span className="form-label" title={slot}>{slot}</span>
            <span className="form-control object-material">
              <select
                value={assigned}
                onChange={(e) => void assign(slot, e.target.value || null)}
                title="割り当てていなければ、取り込んだ値 (モデルのスロットの基本色とテクスチャ) の標準のマテリアルで描く"
              >
                <option value="">(取り込んだ値)</option>
                {assigned && !materialFiles.includes(assigned) && <option value={assigned}>{assigned} (ない)</option>}
                {materialFiles.map((f) => (
                  <option key={f} value={f}>
                    {f.replace(/^materials\//, '')}
                  </option>
                ))}
              </select>
              {assigned ? (
                <button onClick={() => openMaterialTab(assigned)} title="マテリアルのタブで編集する">開く</button>
              ) : (
                <button onClick={() => void makeMaterial(slot, i)} title="取り込んだ値をマテリアルのファイル (materials/) に書き出して、このスロットに割り当てる">
                  マテリアルにする
                </button>
              )}
            </span>
          </div>
        );
      })}
    </div>
  );
}

// 表示用に丸める (0.1 + 0.2 のような誤差を出さない)
function round(v: number): number {
  return Math.round(v * 10000) / 10000;
}
