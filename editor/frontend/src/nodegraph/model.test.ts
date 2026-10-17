// ============================================================
//  ノードエディターのモデルのテスト (Vitest。npm run test)
// ============================================================
import { describe, expect, it } from 'vitest';
import type { JsonObject } from '../bridge/protocol';
import { asArray, asObject, passesOf } from '../state/graph';
import {
  addPass,
  addResource,
  autoLayout,
  buildModel,
  connect,
  previewTargets,
  removeEdges,
  removeNodes,
  renamePass,
  renameResource,
  savedPositions,
  withPositions,
} from './model';

// テンプレート (editor/templates/deferred/graphs/viewport.json) と同じ形
const graph: JsonObject = {
  version: 1,
  resources: {
    gbAlbedo: { format: 'R8G8B8A8_Unorm', size: { relative: 1 } },
    gbNormal: { format: 'R16G16B16A16_Float', size: { relative: 1 } },
    depth: { format: 'D32_Float', size: { relative: 1 }, clear: { depth: 1 } },
    sceneColor: { format: 'R16G16B16A16_Float', size: { relative: 1 } },
  },
  passes: [
    {
      name: 'GBuffer', type: 'Raster', pipeline: 'pipelines/gbuffer.json',
      outputs: [{ res: 'gbAlbedo', as: 'RTV0', load: 'clear' }, { res: 'gbNormal', as: 'RTV1', load: 'clear' }, { res: 'depth', as: 'DSV', load: 'clear' }],
    },
    {
      name: 'Lighting', type: 'Raster', pipeline: 'pipelines/lighting.json',
      inputs: [{ res: 'gbAlbedo', as: 'SRV', bind: { param: 1 } }, { res: 'gbNormal', as: 'SRV', bind: { param: 1, offset: 1 } }, { res: 'depth', as: 'SRV', bind: { param: 1, offset: 2 } }],
      outputs: [{ res: 'sceneColor', as: 'RTV0', load: 'dontCare' }],
    },
    {
      name: 'Composite', type: 'Raster', pipeline: 'pipelines/composite.json', after: ['Lighting'],
      inputs: [{ res: 'sceneColor', as: 'SRV', bind: { param: 1 } }],
      outputs: [{ res: '$backbuffer', as: 'RTV0', load: 'dontCare' }],
    },
  ],
};

const pass = (g: JsonObject, name: string) => passesOf(g).find((p) => p.name === name)!;
const refs = (g: JsonObject, name: string, key: 'inputs' | 'outputs') => asArray(pass(g, name)[key]).map(asObject);

describe('buildModel', () => {
  it('パスとリソース (インポートを含む) をノードに、読み書きを線にする', () => {
    const m = buildModel(graph);
    const ids = m.nodes.map((n) => n.id).sort();
    expect(ids).toEqual(['pass:Composite', 'pass:GBuffer', 'pass:Lighting', 'res:$backbuffer', 'res:depth', 'res:gbAlbedo', 'res:gbNormal', 'res:sceneColor'].sort());
    expect(m.edges).toHaveLength(3 + 3 + 1 + 1 + 1);
    const w = m.edges.find((e) => e.id === 'w:GBuffer:2')!;
    expect(w).toMatchObject({ source: 'pass:GBuffer', sourceHandle: 'out:2', target: 'res:depth', targetHandle: 'in', label: 'DSV', write: true });
    const r = m.edges.find((e) => e.id === 'r:Lighting:1')!;
    expect(r).toMatchObject({ source: 'res:gbNormal', sourceHandle: 'out', target: 'pass:Lighting', targetHandle: 'in:1', label: 'SRV' });
    const lighting = m.nodes.find((n) => n.id === 'pass:Lighting')!.data;
    expect(lighting.kind === 'pass' && lighting.inputs[1].bind).toBe('p1:1');
  });

  it('自動配置は書く → 読むの順に左から右へ並べる', () => {
    const p = autoLayout(buildModel(graph));
    expect(p['pass:GBuffer'][0]).toBeLessThan(p['res:gbAlbedo'][0]);
    expect(p['res:gbAlbedo'][0]).toBeLessThan(p['pass:Lighting'][0]);
    expect(p['pass:Lighting'][0]).toBeLessThan(p['pass:Composite'][0]);
    // 位置のあるノードは保つ
    const kept = autoLayout(buildModel(graph), { 'pass:GBuffer': [999, 999] }, true);
    expect(kept['pass:GBuffer']).toEqual([999, 999]);
  });

  it('位置は editor.nodes に保存して読める', () => {
    const g = withPositions(graph, { 'pass:GBuffer': [10.4, 20.6] });
    expect(savedPositions(g)).toEqual({ 'pass:GBuffer': [10, 21] });
    expect(g.resources).toBe(graph.resources);   // 位置以外は変えない
  });
});

describe('connect', () => {
  it('リソース → パスで入力を加える (bind は一番多い param の次の offset)', () => {
    const r = connect(graph, 'res:sceneColor', 'out', 'pass:Lighting', 'in:new');
    if ('error' in r) throw new Error(r.error);
    const inputs = refs(r.graph, 'Lighting', 'inputs');
    expect(inputs).toHaveLength(4);
    expect(inputs[3]).toEqual({ res: 'sceneColor', as: 'SRV', bind: { param: 1, offset: 3 } });
    expect(refs(graph, 'Lighting', 'inputs')).toHaveLength(3);   // 元は変えない
  });

  it('既存の入力のハンドルにつなぐとリソースを差し替える', () => {
    const r = connect(graph, 'res:sceneColor', 'out', 'pass:Lighting', 'in:0');
    if ('error' in r) throw new Error(r.error);
    expect(refs(r.graph, 'Lighting', 'inputs')[0]).toMatchObject({ res: 'sceneColor', as: 'SRV', bind: { param: 1 } });
  });

  it('パス → リソースで出力を加える (深度は DSV、色は空いている RTV)', () => {
    const added = addResource(graph, 'texture', [0, 0]);
    const r = connect(added.graph, 'pass:Lighting', 'out:new', `res:${added.name}`, 'in');
    if ('error' in r) throw new Error(r.error);
    expect(refs(r.graph, 'Lighting', 'outputs')[1]).toEqual({ res: added.name, as: 'RTV1', load: 'clear' });

    const noDepth = removeEdges(graph, ['w:GBuffer:2']);
    const d = connect(noDepth, 'pass:GBuffer', 'out:new', 'res:depth', 'in');
    if ('error' in d) throw new Error(d.error);
    expect(refs(d.graph, 'GBuffer', 'outputs')[2]).toEqual({ res: 'depth', as: 'DSV', load: 'clear' });
  });

  it('パスどうし、リソースどうしはつなげない', () => {
    expect('error' in connect(graph, 'pass:GBuffer', 'out:new', 'pass:Lighting', 'in:new')).toBe(true);
    expect('error' in connect(graph, 'res:depth', 'out', 'res:gbAlbedo', 'in')).toBe(true);
  });
});

describe('削除と追加', () => {
  it('線を消すと入力 / 出力から外す', () => {
    const g = removeEdges(graph, ['r:Lighting:1', 'w:GBuffer:0']);
    expect(refs(g, 'Lighting', 'inputs').map((r) => r.res)).toEqual(['gbAlbedo', 'depth']);
    expect(refs(g, 'GBuffer', 'outputs').map((r) => r.res)).toEqual(['gbNormal', 'depth']);
  });

  it('パスを消すと after と位置からも消す、リソースを消すと参照も消す', () => {
    const withPos = withPositions(graph, { 'pass:Lighting': [1, 2], 'res:depth': [3, 4] });
    const g = removeNodes(withPos, ['pass:Lighting', 'res:depth']);
    expect(passesOf(g).map((p) => p.name)).toEqual(['GBuffer', 'Composite']);
    expect(pass(g, 'Composite').after).toEqual([]);
    expect(asObject(g.resources).depth).toBeUndefined();
    expect(refs(g, 'GBuffer', 'outputs').map((r) => r.res)).toEqual(['gbAlbedo', 'gbNormal']);
    expect(savedPositions(g)).toEqual({});
  });

  it('パスとリソースを、重ならない名前で加える', () => {
    const a = addPass(graph, 'Raster', 'pipelines/composite.json', [5, 6]);
    const b = addPass(a.graph, 'Raster', 'pipelines/composite.json', [7, 8]);
    expect([a.name, b.name]).toEqual(['Pass', 'Pass2']);
    expect(pass(b.graph, 'Pass')).toMatchObject({ type: 'Raster', pipeline: 'pipelines/composite.json', content: { fullscreen: true } });
    expect(savedPositions(b.graph)['pass:Pass2']).toEqual([7, 8]);
    const c = addPass(graph, 'Copy', 'x', [0, 0]);
    expect(pass(c.graph, 'Copy').pipeline).toBeUndefined();
    const t = addResource(graph, 'buffer', [0, 0]);
    expect(asObject(t.graph.resources)[t.name]).toEqual({ kind: 'buffer', bytes: 256 });
  });
});

describe('名前の変更', () => {
  it('パスの名前を変えると after と位置も直す', () => {
    const g = renamePass(withPositions(graph, { 'pass:Lighting': [1, 2] }), 'Lighting', 'Shade');
    if (typeof g === 'string') throw new Error(g);
    expect(passesOf(g).map((p) => p.name)).toEqual(['GBuffer', 'Shade', 'Composite']);
    expect(pass(g, 'Composite').after).toEqual(['Shade']);
    expect(savedPositions(g)).toEqual({ 'pass:Shade': [1, 2] });
    expect(renamePass(graph, 'Lighting', 'GBuffer')).toMatch(/すでに/);
    expect(renamePass(graph, 'Lighting', 'a[0]')).toMatch(/\[/);
  });

  it('リソースの名前を変えると、宣言の順を保ち、入力と出力の参照も直す', () => {
    const g = renameResource(graph, 'gbNormal', 'normals');
    if (typeof g === 'string') throw new Error(g);
    expect(Object.keys(asObject(g.resources))).toEqual(['gbAlbedo', 'normals', 'depth', 'sceneColor']);
    expect(refs(g, 'GBuffer', 'outputs')[1].res).toBe('normals');
    expect(refs(g, 'Lighting', 'inputs')[1].res).toBe('normals');
    expect(renameResource(graph, 'gbNormal', 'depth')).toMatch(/すでに/);
    expect(renameResource(graph, 'gbNormal', '$x')).toMatch(/\$/);
  });
});

describe('ノードの結果の表示', () => {
  it('リソースはそれ自身、パスは出力 → 入力の順にテクスチャを返し、インポートとバッファは除く', () => {
    expect(previewTargets(graph, 'res:depth')).toEqual([{ resource: 'depth', format: 'D32_Float', role: 'resource' }]);
    expect(previewTargets(graph, 'res:$backbuffer')).toEqual([]);
    expect(previewTargets(graph, 'pass:Lighting').map((t) => `${t.role}:${t.resource}`)).toEqual([
      'output:sceneColor',
      'input:gbAlbedo',
      'input:gbNormal',
      'input:depth',
    ]);
    expect(previewTargets(graph, 'pass:Composite').map((t) => t.resource)).toEqual(['sceneColor']);
    const b = addResource(graph, 'buffer', [0, 0]);
    expect(previewTargets(b.graph, `res:${b.name}`)).toEqual([]);
    expect(previewTargets(graph, 'pass:None')).toEqual([]);
  });
});