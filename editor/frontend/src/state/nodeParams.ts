// ============================================================
//  描画ノードのリソースの値 (物体ごと / スロットごと) をシーンに書く関数
//
//    nodeParamsOf     物体 / スロットの、ある描画ノード (layer) の値 ({ 変数: 値 })
//    withNodeParam    1 つの変数を設定する / 外す (undefined)。空になったオブジェクトは消す
//    effectiveValue   決まった値 (取り込んだ値 → 物体 → スロット) と、その出どころ
//  値の決まりは C++ の services/NodeParams.h と同じ (null は「取り込んだ値も使わない」)。
// ============================================================
import type { JsonValue, SceneJson, SceneObjectJson } from '../bridge/protocol';

export type ParamTarget = { slot?: string }; // slot がなければ物体全体
export type ParamSource = 'slot' | 'object' | 'imported' | 'default';

type Values = Record<string, JsonValue>;

export function nodeParamsOf(obj: SceneObjectJson, layer: string, target: ParamTarget): Values {
  if (target.slot !== undefined) return obj.slotNodeParams?.[target.slot]?.[layer] ?? {};
  return obj.nodeParams?.[layer] ?? {};
}

export function withNodeParam(scene: SceneJson, objectId: string, target: ParamTarget, layer: string, name: string, value: JsonValue | undefined): SceneJson {
  const setIn = (values: Values | undefined): Values | undefined => {
    const next = { ...(values ?? {}) };
    if (value === undefined) delete next[name];
    else next[name] = value;
    return Object.keys(next).length ? next : undefined;
  };
  const objects = scene.objects.map((o) => {
    if (o.id !== objectId) return o;
    const next: SceneObjectJson = { ...o };
    if (target.slot === undefined) {
      const layers = { ...(o.nodeParams ?? {}) };
      const v = setIn(layers[layer]);
      if (v) layers[layer] = v;
      else delete layers[layer];
      if (Object.keys(layers).length) next.nodeParams = layers;
      else delete next.nodeParams;
    } else {
      const slots = { ...(o.slotNodeParams ?? {}) };
      const layers = { ...(slots[target.slot] ?? {}) };
      const v = setIn(layers[layer]);
      if (v) layers[layer] = v;
      else delete layers[layer];
      if (Object.keys(layers).length) slots[target.slot] = layers;
      else delete slots[target.slot];
      if (Object.keys(slots).length) next.slotNodeParams = slots;
      else delete next.slotNodeParams;
    }
    return next;
  });
  return { ...scene, objects };
}

// 表示用: 決まった値とその出どころ。slot がなければ物体全体 (取り込んだ値は最初のスロットのものを見せる)
export function effectiveValue(
  obj: SceneObjectJson,
  layer: string,
  target: ParamTarget,
  name: string,
  imported: Record<string, Values>,
): { value: JsonValue | undefined; source: ParamSource } {
  if (target.slot !== undefined) {
    const s = obj.slotNodeParams?.[target.slot]?.[layer];
    if (s && name in s) return s[name] === null ? { value: undefined, source: 'default' } : { value: s[name], source: 'slot' };
  }
  const o = obj.nodeParams?.[layer];
  if (o && name in o) return o[name] === null ? { value: undefined, source: 'default' } : { value: o[name], source: 'object' };
  const slotName = target.slot ?? Object.keys(imported)[0];
  const im = slotName !== undefined ? imported[slotName] : undefined;
  if (im && name in im) return { value: im[name], source: 'imported' };
  return { value: undefined, source: 'default' };
}

// 変数の既定値 (C++ の packCBuffer と同じ: 0、名前に Color を含む float は 1)
export function defaultNumbers(name: string, type: string, count: number): number[] {
  const one = type === 'float' && name.toLowerCase().includes('color');
  return Array.from({ length: count }, () => (one ? 1 : 0));
}

export function toNumbers(v: JsonValue | undefined, count: number): number[] | null {
  if (typeof v === 'number') return [v, ...Array.from({ length: count - 1 }, () => 0)].slice(0, count);
  if (typeof v === 'boolean') return [v ? 1 : 0];
  if (Array.isArray(v) && v.every((x) => typeof x === 'number' || typeof x === 'boolean'))
    return Array.from({ length: count }, (_, i) => Number(v[i] ?? 0));
  return null;
}
