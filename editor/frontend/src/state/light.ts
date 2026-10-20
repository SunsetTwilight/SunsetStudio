// ============================================================
//  ライトの状態と編集の関数 (M5-5)
//
//    loadLightTypes  lights/*.light.json の種類を読み、ストアに入れる
//    newLight        ライトの物体を作る (種類の既定値。平行光源は斜め下を向ける)
//    lightParam      ライトの値 (書いていなければ種類の既定値) を数の配列で
//    withLightParam / withLightType / withLightLinks  シーンのライトを 1 か所だけ変える
//    linkCandidates  照らす物体に選べるもの (ライト以外)
// ============================================================
import { call, isConnected } from '../bridge/client';
import type { JsonValue, LightTypeInfo, SceneJson, SceneLightJson, SceneObjectJson } from '../bridge/protocol';
import { uniqueId } from './scene';
import { studioStore } from './store';

export async function loadLightTypes(): Promise<LightTypeInfo[]> {
  if (!isConnected()) return [];
  try {
    const types = (await call('light.types')).types;
    studioStore.setLightTypes(types);
    return types;
  } catch {
    studioStore.setLightTypes([]);
    return [];
  }
}

export function newLight(scene: SceneJson, type: LightTypeInfo, parent: string | null = null): SceneObjectJson {
  const directional = type.shape === 'directional';
  return {
    id: uniqueId(scene, type.id.replace(/[^A-Za-z0-9_-]/g, '') || 'light'),
    name: type.label || type.id,
    model: '',
    parent,
    position: directional ? [0, 8, 0] : [0, 3, 0],
    rotation: directional ? [-50, 30, 0] : [-90, 0, 0], // ローカルの -Z が光の向き (スポットは真下)
    scale: [1, 1, 1],
    visible: true,
    color: [1, 1, 1, 1],
    light: { type: type.id, params: {} },
  };
}

export function lightParam(obj: SceneObjectJson, type: LightTypeInfo | undefined, name: string): number[] {
  const def = type?.params.find((p) => p.name === name);
  const fallback = def ? (Array.isArray(def.default) ? def.default : [def.default]) : [0];
  const raw = obj.light?.params?.[name];
  if (typeof raw === 'number') return [raw];
  if (Array.isArray(raw) && raw.length === fallback.length && raw.every((x) => typeof x === 'number')) return raw as number[];
  return [...fallback];
}

function mapLight(scene: SceneJson, id: string, fn: (l: NonNullable<SceneObjectJson['light']>) => NonNullable<SceneObjectJson['light']>): SceneJson {
  return { ...scene, objects: scene.objects.map((o) => (o.id === id && o.light ? { ...o, light: fn(o.light) } : o)) };
}

export function withLightParam(scene: SceneJson, id: string, name: string, values: number[]): SceneJson {
  const v: JsonValue = values.length === 1 ? values[0] : values;
  return mapLight(scene, id, (l) => ({ ...l, params: { ...(l.params ?? {}), [name]: v } }));
}

// 種類を替えると、値は新しい種類の既定値に戻す (名前が同じものは残す)
export function withLightType(scene: SceneJson, id: string, type: LightTypeInfo): SceneJson {
  return mapLight(scene, id, (l) => {
    const keep = Object.fromEntries(Object.entries(l.params ?? {}).filter(([k]) => type.params.some((p) => p.name === k)));
    return { ...l, type: type.id, params: keep };
  });
}

export function withLightLinks(scene: SceneJson, id: string, links: string[]): SceneJson {
  return mapLight(scene, id, (l) => {
    const next: SceneLightJson = { ...l };
    if (links.length > 0) next.links = links;
    else delete next.links;
    return next;
  });
}

export function linkCandidates(scene: SceneJson): SceneObjectJson[] {
  return scene.objects.filter((o) => !o.light);
}
