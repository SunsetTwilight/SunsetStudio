// ============================================================
//  ビューポートのレンダーグラフの読み込みと編集
//
//    loadGraph()        graph.get の結果をストアに入れる (起動時と graph.changed のとき)
//    applyInputGraph()  入力を加えたグラフを送る。先にパイプラインのテーブルに入力の空きを確保する (なければ足す)
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

// 失敗したらエラーの文字列、成功したら null。label は履歴 (元に戻す / やり直す) に出す名前
export async function applyGraph(next: JsonObject, label?: string): Promise<string | null> {
  try {
    studioStore.setGraph(await call('graph.set', { graph: next, label }));
    return null;
  } catch (e) {
    return errorText(e);
  }
}

// パス passName の最後の入力を加えたグラフ next を送る。SRV の入力なら、先に pipeline.allocInput でパイプラインの
// 記述子テーブルに空きを確保し (広げる / なければテーブルを足す)、返った param / offset をその入力の bind にする
// (履歴は「パイプライン」と「グラフ」の 2 つになる。シェーダーで使うには Texture2D の宣言が要る)
export async function applyInputGraph(next: JsonObject, passName: string, label: string): Promise<string | null> {
  const pass = findPass(next, passName);
  const inputs = asArray(pass?.inputs).map(asObject);
  const last = inputs[inputs.length - 1];
  const pipeline = String(pass?.pipeline ?? '');
  let graph = next;
  let allocated = false;
  if (pipeline && last && last.as === 'SRV') {
    const bind = asObject(last.bind);
    try {
      const r = await call('pipeline.allocInput', {
        id: pipeline,
        pass: passName,
        param: typeof bind.param === 'number' ? bind.param : -1,
        offset: Number(bind.offset ?? 0),
        label: `${pipeline} に入力の場所を足す (${passName})`,
      });
      allocated = r.changed;
      if (r.param !== bind.param || r.offset !== Number(bind.offset ?? 0))
        graph = withPass(next, passName, (p) => {
          const ins = asArray(p.inputs).map(asObject);
          ins[ins.length - 1] = { ...ins[ins.length - 1], bind: { param: r.param, offset: r.offset } };
          return { ...p, inputs: ins };
        });
    } catch (e) {
      return errorText(e);
    }
  }
  const err = await applyGraph(graph, label);
  // グラフが通らなければ、確保したパイプラインの変更も戻す (履歴に残さない)
  if (err && allocated) await call('edit.undo').catch(() => {});
  return err;
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
