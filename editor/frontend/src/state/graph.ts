// ============================================================
//  ビューポートのレンダーグラフの読み込みと編集
//
//    loadGraph()        graph.get の結果をストアに入れる (起動時と graph.changed のとき)
//    applyGraph(json)   グラフ全体の JSON を graph.set で送る。コンパイルできなければエラーで、
//                       エディター側は前のグラフのまま (ストアも変えない)
//  グラフの JSON の形はエンジンの graphToJson と同じ (resources はオブジェクト、passes は配列)。
// ============================================================
import { call, StudioError } from '../bridge/client';
import type { JsonObject, JsonValue } from '../bridge/protocol';
import { studioStore } from './store';

export function errorText(e: unknown): string {
  return e instanceof StudioError ? e.message : String(e);
}

export async function loadGraph(): Promise<void> {
  studioStore.setGraph(await call('graph.get'));
}

// 失敗したらエラーの文字列、成功したら null
export async function applyGraph(next: JsonObject): Promise<string | null> {
  try {
    studioStore.setGraph(await call('graph.set', { graph: next }));
    return null;
  } catch (e) {
    return errorText(e);
  }
}

export function asObject(v: JsonValue | undefined): JsonObject {
  return v && typeof v === 'object' && !Array.isArray(v) ? v : {};
}

export function asArray(v: JsonValue | undefined): JsonValue[] {
  return Array.isArray(v) ? v : [];
}

export function passesOf(graph: JsonObject): JsonObject[] {
  return asArray(graph.passes).map(asObject);
}

export function findPass(graph: JsonObject, name: string): JsonObject | undefined {
  return passesOf(graph).find((p) => p.name === name);
}

// name のパスを update で置き換えたグラフ
export function withPass(graph: JsonObject, name: string, update: (pass: JsonObject) => JsonObject): JsonObject {
  return { ...graph, passes: passesOf(graph).map((p) => (p.name === name ? update(p) : p)) };
}

// name のリソースを update で置き換えたグラフ
export function withResource(graph: JsonObject, name: string, update: (res: JsonObject) => JsonObject): JsonObject {
  const resources = { ...asObject(graph.resources) };
  resources[name] = update(asObject(resources[name]));
  return { ...graph, resources };
}

// パスが参照できるリソースの名前 (宣言したもの + グラフの中で使われている $ のインポート)
export function resourceNames(graph: JsonObject): string[] {
  const names = new Set(Object.keys(asObject(graph.resources)));
  for (const p of passesOf(graph))
    for (const key of ['inputs', 'outputs'])
      for (const r of asArray(p[key]).map(asObject))
        if (typeof r.res === 'string' && r.res.startsWith('$')) names.add(r.res);
  return [...names];
}
