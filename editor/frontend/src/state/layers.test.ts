// ============================================================
//  描画レイヤーの関数のテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import type { JsonObject, SceneJson } from '../bridge/protocol';
import { graphDrawLayers, isUndrawn, objectDrawLayers, slotDrawLayers, toggleLayer, withObjectLayers, withSlotLayers } from './layers';
import { newObject } from './scene';

const graph: JsonObject = {
  passes: [
    { name: 'Shadow', content: { drawList: { layer: 'Shadow' } } },
    { name: 'GBuffer', content: { drawList: { layer: 'Opaque' } } },
    { name: 'Lighting', content: { fullscreen: true } },
    { name: 'Toon', content: { drawList: { layer: 'Toon' } } },
    { name: 'ToonOff', enabled: false, content: { drawList: { layer: 'Off' } } },
    { name: 'Outline', content: { drawList: { layer: 'Toon' } } },
    { name: 'Grid', content: { drawList: { layer: 'Grid' } } },
  ],
};

describe('描画レイヤー', () => {
  it('グラフから選べるレイヤー (Shadow / Grid / 無効なパスは除く)', () => {
    expect(graphDrawLayers(graph)).toEqual([
      { layer: 'Opaque', passes: ['GBuffer'] },
      { layer: 'Toon', passes: ['Toon', 'Outline'] },
    ]);
    expect(graphDrawLayers(null)).toEqual([]);
  });

  it('既定、物体の指定、スロットの上書き', () => {
    const base = newObject({ version: 1, objects: [] });
    expect(objectDrawLayers(base)).toEqual(['Opaque']);
    let scene: SceneJson = { version: 1, objects: [base] };
    scene = withObjectLayers(scene, base.id, ['Toon']);
    scene = withSlotLayers(scene, base.id, 'Hair', []);
    const obj = scene.objects[0];
    expect(slotDrawLayers(obj, 'Body')).toEqual(['Toon']);
    expect(slotDrawLayers(obj, 'Hair')).toEqual([]);
    // 戻すとキーを消す
    const back = withSlotLayers(withObjectLayers(scene, base.id, undefined), base.id, 'Hair', undefined).objects[0];
    expect('layers' in back).toBe(false);
    expect('slotLayers' in back).toBe(false);
  });

  it('どこにも描かれないか / 足す・外す', () => {
    const avail = graphDrawLayers(graph);
    expect(isUndrawn([], avail)).toBe(true);
    expect(isUndrawn(['Missing'], avail)).toBe(true);
    expect(isUndrawn(['Missing', 'Toon'], avail)).toBe(false);
    expect(toggleLayer(['Missing', 'Toon'], 'Opaque', true, avail)).toEqual(['Opaque', 'Toon', 'Missing']);
    expect(toggleLayer(['Opaque', 'Toon'], 'Opaque', false, avail)).toEqual(['Toon']);
  });
});
