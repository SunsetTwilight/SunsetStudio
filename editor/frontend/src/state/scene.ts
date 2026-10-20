// ============================================================
//  シーン (M5-2): 何を描くか。ファイルは scenes/*.scene.json (C++ の services/Scene.h と同じ形)
//
//    loadScene()          scene.get の結果をストアに入れる (プロジェクトを開いたときと scene.changed のとき)
//    applyScene(json)     シーン全体を scene.set で送る (元に戻す / やり直すの対象。保存は scene.save)
//    以下は JSON を受け取って新しい JSON を返す編集の関数 (Vitest で試す)
// ============================================================
import { call } from '../bridge/client';
import type { SceneJson, SceneObjectJson } from '../bridge/protocol';
import { errorText } from './graph';
import { studioStore } from './store';

export async function loadScene(): Promise<void> {
  try {
    studioStore.setScene(await call('scene.get'));
  } catch {
    studioStore.setScene(null);
  }
}

// 失敗したらエラーの文字列、成功したら null
export async function applyScene(next: SceneJson, label: string): Promise<string | null> {
  try {
    const r = await call('scene.set', { scene: next, label });
    const cur = studioStore.get().scene;
    if (cur) studioStore.setScene({ ...cur, scene: next, revision: r.revision, dirty: r.dirty });
    return null;
  } catch (e) {
    return errorText(e);
  }
}

export function newObject(scene: SceneJson, prefix = 'cube'): SceneObjectJson {
  return {
    id: uniqueId(scene, prefix),
    name: 'Cube',
    model: 'builtin:cube',
    parent: null,
    position: [0, 0.5, 0],
    rotation: [0, 0, 0],
    scale: [1, 1, 1],
    visible: true,
    color: [0.8, 0.8, 0.8, 1],
  };
}

export function uniqueId(scene: SceneJson, prefix: string): string {
  const used = new Set(scene.objects.map((o) => o.id));
  for (let n = 1; ; ++n) if (!used.has(`${prefix}-${n}`)) return `${prefix}-${n}`;
}

// 物体を加える (parent の子にする。選んでいる物の隣に置くときなど)
export function addObject(scene: SceneJson, obj: SceneObjectJson): SceneJson {
  return { ...scene, objects: [...scene.objects, obj] };
}

export function updateObject(scene: SceneJson, id: string, patch: Partial<SceneObjectJson>): SceneJson {
  return { ...scene, objects: scene.objects.map((o) => (o.id === id ? { ...o, ...patch } : o)) };
}

// 子孫の id (自分は含まない)
export function descendants(scene: SceneJson, id: string): Set<string> {
  const out = new Set<string>();
  let frontier = [id];
  while (frontier.length) {
    const next: string[] = [];
    for (const o of scene.objects) if (o.parent && frontier.includes(o.parent) && !out.has(o.id)) (out.add(o.id), next.push(o.id));
    frontier = next;
  }
  return out;
}

// 親にできる物体 (自分と子孫は除く。循環を作らない)
export function parentCandidates(scene: SceneJson, id: string): SceneObjectJson[] {
  const bad = descendants(scene, id);
  bad.add(id);
  return scene.objects.filter((o) => !bad.has(o.id));
}

// 消す: 子は消した物の親につなぎ直す (位置はローカルのまま)
export function removeObject(scene: SceneJson, id: string): SceneJson {
  const target = scene.objects.find((o) => o.id === id);
  if (!target) return scene;
  return {
    ...scene,
    objects: scene.objects.filter((o) => o.id !== id).map((o) => (o.parent === id ? { ...o, parent: target.parent } : o)),
  };
}

// 複製する (子は複製しない)。少しずらして同じ親に置く
export function duplicateObject(scene: SceneJson, id: string): { scene: SceneJson; id: string } | null {
  const src = scene.objects.find((o) => o.id === id);
  if (!src) return null;
  const prefix = src.id.replace(/[-_]?\d+$/, '') || 'object';
  const copy: SceneObjectJson = { ...src, id: uniqueId(scene, prefix), name: `${src.name} (複製)`, position: [src.position[0] + 1, src.position[1], src.position[2]] };
  const at = scene.objects.indexOf(src) + 1;
  return { scene: { ...scene, objects: [...scene.objects.slice(0, at), copy, ...scene.objects.slice(at)] }, id: copy.id };
}

// シーンのパネルの木 (親の順、同じ親の中はファイルの順)
export interface TreeRow {
  obj: SceneObjectJson;
  depth: number;
  hasChildren: boolean;
}
export function sceneTree(scene: SceneJson, collapsed: Set<string> = new Set()): TreeRow[] {
  const ids = new Set(scene.objects.map((o) => o.id));
  const children = new Map<string, SceneObjectJson[]>();
  for (const o of scene.objects) {
    const p = o.parent && ids.has(o.parent) ? o.parent : '';
    if (!children.has(p)) children.set(p, []);
    children.get(p)!.push(o);
  }
  const out: TreeRow[] = [];
  const walk = (parent: string, depth: number) => {
    for (const o of children.get(parent) ?? []) {
      const kids = children.get(o.id) ?? [];
      out.push({ obj: o, depth, hasChildren: kids.length > 0 });
      if (!collapsed.has(o.id)) walk(o.id, depth + 1);
    }
  };
  walk('', 0);
  return out;
}

// 色 (0〜1 の RGB) ⇔ "#rrggbb"
export function toHex(c: number[]): string {
  const h = (x: number) => Math.round(Math.min(Math.max(x, 0), 1) * 255).toString(16).padStart(2, '0');
  return `#${h(c[0] ?? 0)}${h(c[1] ?? 0)}${h(c[2] ?? 0)}`;
}
export function fromHex(hex: string, alpha = 1): [number, number, number, number] {
  const n = parseInt(hex.slice(1), 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255, alpha];
}
