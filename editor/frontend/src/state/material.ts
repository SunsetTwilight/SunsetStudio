// ============================================================
//  マテリアルの状態と編集の関数 (M5-4)
//
//    loadMaterialFiles   materials/*.material.json の一覧を読み、ストアに入れる (material.changed のたびに)
//    objectSlots         物体のマテリアルのスロット (立方体は "Default"、モデルはライブラリのスロットの名前)
//    autoMaterialKey     割り当てていないスロットの、取り込んだ値のマテリアルのキー
//    withAssignment      シーンの物体のスロットに、マテリアルのファイルを割り当てる / 外す
//    newMaterialPath     重ならない新しいファイルの名前 (materials/<名前>-<スロット>.material.json)
//    withParam / withTexture / withPipeline  マテリアルの JSON を 1 か所だけ変える
//    isColorParam        色の欄にする変数か (名前に Color を含む float3 / float4)
// ============================================================
import { call, isConnected } from '../bridge/client';
import type { JsonValue, LibraryInfo, MaterialJson, MaterialVar, SceneJson, SceneObjectJson } from '../bridge/protocol';
import { assetOfModel, idPrefix } from './library';
import { studioStore } from './store';

export async function loadMaterialFiles(): Promise<string[]> {
  if (!isConnected()) return [];
  try {
    const files = (await call('material.list')).files;
    studioStore.setMaterialFiles(files);
    return files;
  } catch {
    studioStore.setMaterialFiles([]);
    return [];
  }
}

export function objectSlots(obj: SceneObjectJson, library: LibraryInfo | null): string[] {
  if (obj.model === 'builtin:cube') return ['Default'];
  return assetOfModel(library, obj.model)?.model?.info?.slotNames ?? [];
}

export function autoMaterialKey(obj: SceneObjectJson, slotIndex: number): string {
  if (obj.model === 'builtin:cube') return 'auto:cube';
  return `auto:${obj.model.replace(/^asset:/, '')}:${slotIndex}`;
}

export function withAssignment(scene: SceneJson, objectId: string, slot: string, path: string | null): SceneJson {
  return {
    ...scene,
    objects: scene.objects.map((o) => {
      if (o.id !== objectId) return o;
      const materials = { ...(o.materials ?? {}) };
      if (path) materials[slot] = path;
      else delete materials[slot];
      const next: SceneObjectJson = { ...o, materials };
      if (Object.keys(materials).length === 0) delete next.materials;
      return next;
    }),
  };
}

export function newMaterialPath(name: string, slot: string, existing: string[]): string {
  const base = `${idPrefix(name)}-${idPrefix(slot)}`;
  const used = new Set(existing);
  for (let n = 1; ; ++n) {
    const p = `materials/${base}${n === 1 ? '' : `-${n}`}.material.json`;
    if (!used.has(p)) return p;
  }
}

export function withParam(m: MaterialJson, name: string, value: JsonValue): MaterialJson {
  return { ...m, params: { ...m.params, [name]: value } };
}

export function withTexture(m: MaterialJson, name: string, ref: string | null): MaterialJson {
  const textures = { ...m.textures };
  if (ref) textures[name] = ref;
  else delete textures[name];
  return { ...m, textures };
}

export function withPipeline(m: MaterialJson, pass: string, pipeline: string): MaterialJson {
  return { ...m, passes: { ...m.passes, [pass]: pipeline } };
}

export function isColorParam(v: MaterialVar): boolean {
  return v.type === 'float' && v.rows === 1 && v.elements === 0 && (v.columns === 3 || v.columns === 4) && /color/i.test(v.name);
}

// パラメーターの今の値を、変数の形 (要素の数) の数の配列にする (足りなければ 0)
export function paramValues(m: MaterialJson, v: MaterialVar): number[] {
  const n = v.columns * Math.max(v.elements, 1);
  const raw = m.params[v.name];
  const arr = Array.isArray(raw) ? raw : raw === undefined || raw === null ? [] : [raw];
  return Array.from({ length: n }, (_, i) => {
    const x = arr[i];
    return typeof x === 'number' ? x : typeof x === 'boolean' ? (x ? 1 : 0) : 0;
  });
}

// 数の配列を、変数の形に合わせて JSON の値にする (1 個なら数、bool なら真偽値)
export function paramValue(v: MaterialVar, values: number[]): JsonValue {
  if (values.length === 1) return v.type === 'bool' ? values[0] !== 0 : values[0];
  return values;
}
