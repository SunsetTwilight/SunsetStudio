// ============================================================
//  描画レイヤー: シーンの物体 (やスロット) のメッシュを、どの描画ノード (グラフの drawList の layer) で描くか
//  (C++ の services/Scene.h の drawLayersOf と同じ決まり)
//
//    スロットの上書き (slotLayers) → 物体の layers → 既定の "Opaque"。空の配列 = どこにも描かない
//    どのノードにも入らないメッシュは、ビューポートでピンクに表示される
//    Shadow (影を落とすかで決まる) と Grid (エディターのグリッド) は選ぶ対象にしない
// ============================================================
import type { JsonObject, SceneJson, SceneObjectJson } from '../bridge/protocol';

export const DEFAULT_DRAW_LAYER = 'Opaque';
const SPECIAL_LAYERS = new Set(['Shadow', 'Grid']);

// グラフで選べる描画レイヤーと、それを描くノード (有効なパスだけ。グラフの順)
export interface DrawLayerInfo {
  layer: string;
  passes: string[];
}

export function graphDrawLayers(graph: JsonObject | null | undefined): DrawLayerInfo[] {
  const out: DrawLayerInfo[] = [];
  const passes = Array.isArray(graph?.passes) ? graph.passes : [];
  for (const p of passes) {
    if (!p || typeof p !== 'object' || Array.isArray(p)) continue;
    if (p.enabled === false) continue;
    const content = p.content;
    if (!content || typeof content !== 'object' || Array.isArray(content)) continue;
    const drawList = content.drawList;
    if (!drawList || typeof drawList !== 'object' || Array.isArray(drawList)) continue;
    const layer = typeof drawList.layer === 'string' ? drawList.layer : '';
    if (!layer || SPECIAL_LAYERS.has(layer)) continue;
    const name = typeof p.name === 'string' ? p.name : '';
    const found = out.find((x) => x.layer === layer);
    if (found) found.passes.push(name);
    else out.push({ layer, passes: [name] });
  }
  return out;
}

// 物体の描画レイヤー (書いていなければ既定)
export function objectDrawLayers(obj: SceneObjectJson): string[] {
  return obj.layers ?? [DEFAULT_DRAW_LAYER];
}

// スロットの描画レイヤー (上書きがなければ物体の)
export function slotDrawLayers(obj: SceneObjectJson, slot: string): string[] {
  return obj.slotLayers?.[slot] ?? objectDrawLayers(obj);
}

// どのノードにも描かれない (ピンクになる)
export function isUndrawn(layers: string[], available: DrawLayerInfo[]): boolean {
  return !layers.some((l) => available.some((a) => a.layer === l));
}

// i 番目のレイヤーを替える (ほかの行と同じなら、その行を消して重ねない)
export function setLayerAt(layers: string[], index: number, layer: string): string[] {
  const next = layers.map((l, i) => (i === index ? layer : l));
  return next.filter((l, i) => l !== layer || i === index);
}

// i 番目のレイヤーを外す
export function removeLayerAt(layers: string[], index: number): string[] {
  return layers.filter((_, i) => i !== index);
}

// 追加ボタンで足すレイヤー: まだ使っていない、グラフの最初のレイヤー (なければ null)
export function nextUnusedLayer(layers: string[], available: DrawLayerInfo[]): string | null {
  return available.find((a) => !layers.includes(a.layer))?.layer ?? null;
}

// 物体の layers を変える (undefined = 既定に戻す)
export function withObjectLayers(scene: SceneJson, objectId: string, layers: string[] | undefined): SceneJson {
  return {
    ...scene,
    objects: scene.objects.map((o) => {
      if (o.id !== objectId) return o;
      const next: SceneObjectJson = { ...o, layers };
      if (layers === undefined) delete next.layers;
      return next;
    }),
  };
}

// スロットの上書きを変える (undefined = 物体と同じに戻す)
export function withSlotLayers(scene: SceneJson, objectId: string, slot: string, layers: string[] | undefined): SceneJson {
  return {
    ...scene,
    objects: scene.objects.map((o) => {
      if (o.id !== objectId) return o;
      const slotLayers = { ...(o.slotLayers ?? {}) };
      if (layers) slotLayers[slot] = layers;
      else delete slotLayers[slot];
      const next: SceneObjectJson = { ...o, slotLayers };
      if (Object.keys(slotLayers).length === 0) delete next.slotLayers;
      return next;
    }),
  };
}
