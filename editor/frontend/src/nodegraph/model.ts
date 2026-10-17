// ============================================================
//  ノードエディターのモデル: グラフの JSON ⇔ ノードと線、編集の関数
//
//  ノード
//    pass:<名前>  パス。左に入力 (in:<番号>、新しい入力は in:new)、右に出力 (out:<番号>、out:new)
//    res:<名前>   リソース。左 (in) に書くパス、右 (out) から読むパスがつながる。$ で始まるものはインポート
//  線
//    w:<パス>:<番号>  パスの出力 → リソース (書く)
//    r:<パス>:<番号>  リソース → パスの入力 (読む)
//  ノードの位置はグラフの JSON の editor.nodes に { "pass:GBuffer": [x, y] } の形で持つ (エンジンは保つだけ)。
//
//  編集の関数はすべて、グラフの JSON を受け取って新しい JSON を返す (元は変えない)。
//  送るのは state/graph.ts の applyGraph (graph.set) なので、元に戻す / やり直すもそのまま効く。
// ============================================================
import dagre from '@dagrejs/dagre';
import type { JsonObject, JsonValue } from '../bridge/protocol';
import { asArray, asObject, passesOf } from '../state/graph';

export type XY = [number, number];

export interface RefInfo {
  index: number;
  res: string;
  as: string;
  load?: string;
  bind?: string; // "p1:o2"
}

export interface PassNodeData extends Record<string, unknown> {
  kind: 'pass';
  name: string;
  type: string;
  pipeline: string;
  enabled: boolean;
  inputs: RefInfo[];
  outputs: RefInfo[];
}

export interface ResourceNodeData extends Record<string, unknown> {
  kind: 'resource';
  name: string;
  imported: boolean;
  format: string;
  size: string;
  resKind: string;
}

export const PASS_WIDTH = 250;
export const RES_WIDTH = 190;
export const ROW_HEIGHT = 22;

export function passNodeHeight(d: { inputs: unknown[]; outputs: unknown[] }) {
  return 52 + ROW_HEIGHT * (Math.max(d.inputs.length, d.outputs.length) + 1);
}
export const RES_HEIGHT = 58;

export const passId = (name: string) => `pass:${name}`;
export const resId = (name: string) => `res:${name}`;

export function parseNodeId(id: string): { kind: 'pass' | 'resource'; name: string } | null {
  if (id.startsWith('pass:')) return { kind: 'pass', name: id.slice(5) };
  if (id.startsWith('res:')) return { kind: 'resource', name: id.slice(4) };
  return null;
}

function bindText(v: JsonValue | undefined): string | undefined {
  const b = asObject(v);
  if (b.param === undefined) return undefined;
  return `p${String(b.param)}:${String(b.offset ?? 0)}`;
}

function refsOf(pass: JsonObject, key: 'inputs' | 'outputs'): RefInfo[] {
  return asArray(pass[key]).map((v, index) => {
    const r = asObject(v);
    return {
      index,
      res: String(r.res ?? ''),
      as: String(r.as ?? ''),
      load: r.load === undefined ? undefined : String(r.load),
      bind: bindText(r.bind),
    };
  });
}

function sizeText(r: JsonObject): string {
  const s = asObject(r.size);
  if (s.relative !== undefined) return `×${String(s.relative)}`;
  if (s.width !== undefined) return `${String(s.width)}×${String(s.height)}`;
  if (r.bytes !== undefined) return `${String(r.bytes)} B`;
  return '';
}

// ---- グラフ → ノードと線 (位置はまだ付けない) ----
export interface ModelNode {
  id: string;
  data: PassNodeData | ResourceNodeData;
  width: number;
  height: number;
}
export interface ModelEdge {
  id: string;
  source: string;
  sourceHandle: string;
  target: string;
  targetHandle: string;
  label: string;
  write: boolean;
}

export function buildModel(graph: JsonObject): { nodes: ModelNode[]; edges: ModelEdge[] } {
  const nodes: ModelNode[] = [];
  const edges: ModelEdge[] = [];
  const resources = asObject(graph.resources);
  const resourceNames = new Set(Object.keys(resources));

  // インポート ($...) は参照から集める
  for (const p of passesOf(graph))
    for (const key of ['inputs', 'outputs'] as const)
      for (const r of refsOf(p, key)) if (r.res.startsWith('$')) resourceNames.add(r.res);

  for (const name of resourceNames) {
    const r = asObject(resources[name]);
    const imported = name.startsWith('$');
    nodes.push({
      id: resId(name),
      data: {
        kind: 'resource',
        name,
        imported,
        format: imported ? 'インポート' : String(r.format ?? ''),
        size: imported ? '' : sizeText(r),
        resKind: imported ? 'imported' : String(r.kind ?? 'texture'),
      },
      width: RES_WIDTH,
      height: RES_HEIGHT,
    });
  }

  for (const p of passesOf(graph)) {
    const name = String(p.name ?? '');
    const data: PassNodeData = {
      kind: 'pass',
      name,
      type: String(p.type ?? 'Raster'),
      pipeline: String(p.pipeline ?? ''),
      enabled: p.enabled !== false,
      inputs: refsOf(p, 'inputs'),
      outputs: refsOf(p, 'outputs'),
    };
    nodes.push({ id: passId(name), data, width: PASS_WIDTH, height: passNodeHeight(data) });
    for (const r of data.inputs)
      if (resourceNames.has(r.res))
        edges.push({
          id: `r:${name}:${r.index}`,
          source: resId(r.res),
          sourceHandle: 'out',
          target: passId(name),
          targetHandle: `in:${r.index}`,
          label: r.as,
          write: false,
        });
    for (const r of data.outputs)
      if (resourceNames.has(r.res))
        edges.push({
          id: `w:${name}:${r.index}`,
          source: passId(name),
          sourceHandle: `out:${r.index}`,
          target: resId(r.res),
          targetHandle: 'in',
          label: r.as,
          write: true,
        });
  }
  return { nodes, edges };
}

// ---- 位置 ----
export function savedPositions(graph: JsonObject): Record<string, XY> {
  const nodes = asObject(asObject(graph.editor).nodes);
  const out: Record<string, XY> = {};
  for (const [id, v] of Object.entries(nodes)) {
    const a = asArray(v);
    if (a.length === 2 && typeof a[0] === 'number' && typeof a[1] === 'number') out[id] = [a[0], a[1]];
  }
  return out;
}

export function withPositions(graph: JsonObject, positions: Record<string, XY>): JsonObject {
  const editor = asObject(graph.editor);
  const nodes: JsonObject = {};
  for (const [id, [x, y]] of Object.entries(positions)) nodes[id] = [Math.round(x), Math.round(y)];
  return { ...graph, editor: { ...editor, nodes } };
}

// dagre で左 → 右に並べる。keep にある位置は保つ (only = 位置のないノードだけを並べる)
export function autoLayout(model: { nodes: ModelNode[]; edges: ModelEdge[] }, keep: Record<string, XY> = {}, onlyMissing = false): Record<string, XY> {
  const g = new dagre.graphlib.Graph();
  g.setGraph({ rankdir: 'LR', nodesep: 28, ranksep: 70, marginx: 20, marginy: 20 });
  g.setDefaultEdgeLabel(() => ({}));
  for (const n of model.nodes) g.setNode(n.id, { width: n.width, height: n.height });
  for (const e of model.edges) g.setEdge(e.source, e.target);
  dagre.layout(g);
  const out: Record<string, XY> = {};
  for (const n of model.nodes) {
    if (onlyMissing && keep[n.id]) {
      out[n.id] = keep[n.id];
      continue;
    }
    const p = g.node(n.id);
    out[n.id] = [p.x - n.width / 2, p.y - n.height / 2];
  }
  return out;
}

// ---- 編集 ----
function setPasses(graph: JsonObject, passes: JsonObject[]): JsonObject {
  return { ...graph, passes };
}

function isDepthFormat(f: string) {
  return f.startsWith('D');
}

function formatOf(graph: JsonObject, res: string): string {
  return String(asObject(asObject(graph.resources)[res]).format ?? '');
}

// 入力の bind の既定: 今ある SRV / UAV の bind のうち一番多い param の、次の offset
function nextBind(pass: JsonObject): JsonObject {
  const counts = new Map<number, number>();
  const maxOffset = new Map<number, number>();
  for (const key of ['inputs', 'outputs'])
    for (const v of asArray(pass[key])) {
      const b = asObject(asObject(v).bind);
      if (typeof b.param !== 'number') continue;
      counts.set(b.param, (counts.get(b.param) ?? 0) + 1);
      maxOffset.set(b.param, Math.max(maxOffset.get(b.param) ?? -1, Number(b.offset ?? 0)));
    }
  let param = 0;
  let best = -1;
  for (const [p, c] of counts) if (c > best) [param, best] = [p, c];
  return { param, offset: (maxOffset.get(param) ?? -1) + 1 };
}

// 出力の使い方の既定: 深度なら DSV (まだなければ)、それ以外は空いている RTV
function nextOutputAs(pass: JsonObject, format: string): string {
  const used = new Set(asArray(pass.outputs).map((v) => String(asObject(v).as ?? '')));
  if (pass.type === 'Compute') return 'UAV';
  if (pass.type === 'Copy') return 'CopyDst';
  if (isDepthFormat(format) && !used.has('DSV')) return 'DSV';
  for (let i = 0; i < 8; ++i) if (!used.has(`RTV${i}`)) return `RTV${i}`;
  return 'RTV0';
}

export type ConnectResult = { graph: JsonObject; label: string } | { error: string };

// source (ノードとハンドル) → target をつなぐ。リソース → パスの入力、パスの出力 → リソースだけ
export function connect(graph: JsonObject, source: string, sourceHandle: string, target: string, targetHandle: string): ConnectResult {
  const s = parseNodeId(source);
  const t = parseNodeId(target);
  if (!s || !t || s.kind === t.kind) return { error: 'パスとリソースのあいだだけをつなげます' };

  if (s.kind === 'resource' && t.kind === 'pass') {
    // 読む: リソース → パスの入力
    const res = s.name;
    const next = passesOf(graph).map((p) => {
      if (p.name !== t.name) return p;
      const inputs = asArray(p.inputs).map(asObject);
      if (targetHandle.startsWith('in:') && targetHandle !== 'in:new') {
        const i = Number(targetHandle.slice(3));
        return { ...p, inputs: inputs.map((r, k) => (k === i ? { ...r, res } : r)) };
      }
      const as = p.type === 'Copy' ? 'CopySrc' : 'SRV';
      const ref: JsonObject = { res, as };
      if (as === 'SRV') ref.bind = nextBind(p);
      return { ...p, inputs: [...inputs, ref] };
    });
    return { graph: setPasses(graph, next), label: `${res} → ${t.name} (入力)` };
  }

  // 書く: パスの出力 → リソース
  const res = t.name;
  const pass = s.name;
  const next = passesOf(graph).map((p) => {
    if (p.name !== pass) return p;
    const outputs = asArray(p.outputs).map(asObject);
    if (sourceHandle.startsWith('out:') && sourceHandle !== 'out:new') {
      const i = Number(sourceHandle.slice(4));
      return { ...p, outputs: outputs.map((r, k) => (k === i ? { ...r, res } : r)) };
    }
    const as = nextOutputAs(p, formatOf(graph, res));
    const ref: JsonObject = { res, as };
    if (as.startsWith('RTV') || as === 'DSV') ref.load = 'clear';
    if (as === 'UAV') ref.bind = nextBind(p);
    return { ...p, outputs: [...outputs, ref] };
  });
  return { graph: setPasses(graph, next), label: `${pass} → ${res} (出力)` };
}

// 線を消す (パスの入力 / 出力から外す)
export function removeEdges(graph: JsonObject, edgeIds: string[]): JsonObject {
  const drop = new Map<string, { inputs: Set<number>; outputs: Set<number> }>();
  for (const id of edgeIds) {
    const m = /^([rw]):(.*):(\d+)$/.exec(id);
    if (!m) continue;
    const entry = drop.get(m[2]) ?? { inputs: new Set<number>(), outputs: new Set<number>() };
    (m[1] === 'r' ? entry.inputs : entry.outputs).add(Number(m[3]));
    drop.set(m[2], entry);
  }
  return setPasses(
    graph,
    passesOf(graph).map((p) => {
      const d = drop.get(String(p.name));
      if (!d) return p;
      return {
        ...p,
        inputs: asArray(p.inputs).filter((_, i) => !d.inputs.has(i)),
        outputs: asArray(p.outputs).filter((_, i) => !d.outputs.has(i)),
      };
    }),
  );
}

// ノードを消す。パスは after の参照も、リソースは参照している入力 / 出力も消す
export function removeNodes(graph: JsonObject, nodeIds: string[]): JsonObject {
  const passes = new Set<string>();
  const resources = new Set<string>();
  for (const id of nodeIds) {
    const n = parseNodeId(id);
    if (n?.kind === 'pass') passes.add(n.name);
    if (n?.kind === 'resource') resources.add(n.name);
  }
  const keepRef = (v: JsonValue) => !resources.has(String(asObject(v).res ?? ''));
  const nextPasses = passesOf(graph)
    .filter((p) => !passes.has(String(p.name)))
    .map((p) => ({
      ...p,
      inputs: asArray(p.inputs).filter(keepRef),
      outputs: asArray(p.outputs).filter(keepRef),
      ...(p.after ? { after: asArray(p.after).filter((a) => !passes.has(String(a))) } : {}),
    }));
  const res = { ...asObject(graph.resources) };
  for (const r of resources) delete res[r];
  const positions = savedPositions(graph);
  for (const id of nodeIds) delete positions[id];
  return withPositions({ ...graph, resources: res, passes: nextPasses }, positions);
}

function uniqueName(base: string, taken: Set<string>): string {
  if (!taken.has(base)) return base;
  for (let i = 2; ; ++i) if (!taken.has(`${base}${i}`)) return `${base}${i}`;
}

export type PassKind = 'Raster' | 'Compute' | 'Copy';

export function addPass(graph: JsonObject, kind: PassKind, pipeline: string, at: XY): { graph: JsonObject; name: string } {
  const name = uniqueName(kind === 'Raster' ? 'Pass' : kind, new Set(passesOf(graph).map((p) => String(p.name))));
  const pass: JsonObject = { name, type: kind };
  if (kind !== 'Copy') pass.pipeline = pipeline;
  if (kind === 'Raster') pass.content = { fullscreen: true };
  if (kind === 'Compute') pass.content = { dispatch: [1, 1, 1] };
  const positions = { ...savedPositions(graph), [passId(name)]: at };
  return { graph: withPositions(setPasses(graph, [...passesOf(graph), pass]), positions), name };
}

export function addResource(graph: JsonObject, kind: 'texture' | 'buffer', at: XY): { graph: JsonObject; name: string } {
  const resources = { ...asObject(graph.resources) };
  const name = uniqueName(kind === 'texture' ? 'tex' : 'buf', new Set(Object.keys(resources)));
  resources[name] = kind === 'texture' ? { format: 'R8G8B8A8_Unorm', size: { relative: 1 } } : { kind: 'buffer', bytes: 256 };
  const positions = { ...savedPositions(graph), [resId(name)]: at };
  return { graph: withPositions({ ...graph, resources }, positions), name };
}

export function renamePass(graph: JsonObject, from: string, to: string): JsonObject | string {
  if (!to.trim()) return '名前が空です';
  if (to.includes('[') || to.includes('@')) return '名前に [ と @ は使えません';
  if (passesOf(graph).some((p) => p.name === to)) return `パス '${to}' はすでにあります`;
  const passes = passesOf(graph).map((p) => ({
    ...p,
    ...(p.name === from ? { name: to } : {}),
    ...(p.after ? { after: asArray(p.after).map((a) => (a === from ? to : a)) } : {}),
  }));
  const positions = savedPositions(graph);
  if (positions[passId(from)]) {
    positions[passId(to)] = positions[passId(from)];
    delete positions[passId(from)];
  }
  return withPositions(setPasses(graph, passes), positions);
}

export function renameResource(graph: JsonObject, from: string, to: string): JsonObject | string {
  if (!to.trim()) return '名前が空です';
  if (to.startsWith('$') || to.includes('@')) return '名前を $ で始めたり、@ を使ったりはできません';
  const resources = asObject(graph.resources);
  if (resources[to] !== undefined) return `リソース '${to}' はすでにあります`;
  // 宣言の順を保って名前を変える
  const nextRes: JsonObject = {};
  for (const [k, v] of Object.entries(resources)) nextRes[k === from ? to : k] = v;
  const fix = (v: JsonValue) => {
    const r = asObject(v);
    return r.res === from ? { ...r, res: to } : v;
  };
  const passes = passesOf(graph).map((p) => {
    const content = asObject(p.content);
    const fixIndirect = (key: string) => {
      const ind = asObject(content[key]);
      if (ind.args === from || ind.count === from)
        return { ...content, [key]: { ...ind, ...(ind.args === from ? { args: to } : {}), ...(ind.count === from ? { count: to } : {}) } };
      return null;
    };
    const nextContent = fixIndirect('drawIndirect') ?? fixIndirect('dispatchIndirect');
    return {
      ...p,
      inputs: asArray(p.inputs).map(fix),
      outputs: asArray(p.outputs).map(fix),
      ...(nextContent ? { content: nextContent } : {}),
    };
  });
  const positions = savedPositions(graph);
  if (positions[resId(from)]) {
    positions[resId(to)] = positions[resId(from)];
    delete positions[resId(from)];
  }
  return withPositions({ ...graph, resources: nextRes, passes }, positions);
}
