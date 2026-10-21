// ============================================================
//  描画ノードのリソースの値の関数のテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import type { SceneJson, SceneObjectJson } from '../bridge/protocol';
import { defaultNumbers, effectiveValue, nodeParamsOf, toNumbers, withNodeParam } from './nodeParams';

const obj = (id: string): SceneObjectJson => ({
  id,
  name: id,
  model: 'builtin:cube',
  parent: null,
  position: [0, 0, 0],
  rotation: [0, 0, 0],
  scale: [1, 1, 1],
  visible: true,
  color: [1, 1, 1, 1],
});

describe('描画ノードのリソース', () => {
  it('物体 / スロットに設定して外す (空になれば消す)', () => {
    let s: SceneJson = { version: 1, objects: [obj('a'), obj('b')] };
    s = withNodeParam(s, 'a', {}, 'Draw', 'roughness', 0.5);
    s = withNodeParam(s, 'a', { slot: 'Hair' }, 'Draw', 'baseColorMap', 'asset:0123456789abcdef0123456789abcdef');
    expect(s.objects[0].nodeParams).toEqual({ Draw: { roughness: 0.5 } });
    expect(s.objects[0].slotNodeParams).toEqual({ Hair: { Draw: { baseColorMap: 'asset:0123456789abcdef0123456789abcdef' } } });
    expect(s.objects[1].nodeParams).toBeUndefined();
    expect(nodeParamsOf(s.objects[0], 'Draw', {})).toEqual({ roughness: 0.5 });
    expect(nodeParamsOf(s.objects[0], 'Draw', { slot: 'Body' })).toEqual({});
    s = withNodeParam(s, 'a', {}, 'Draw', 'roughness', undefined);
    s = withNodeParam(s, 'a', { slot: 'Hair' }, 'Draw', 'baseColorMap', undefined);
    expect(s.objects[0].nodeParams).toBeUndefined();
    expect(s.objects[0].slotNodeParams).toBeUndefined();
  });

  it('決まった値: スロット → 物体 → 取り込んだ値 → 既定', () => {
    const o: SceneObjectJson = { ...obj('a'), nodeParams: { Draw: { roughness: 0.9, alphaCutoff: null } }, slotNodeParams: { Hair: { Draw: { roughness: 0.1 } } } };
    const imported = { Body: { roughness: 0.3, baseColor: [1, 0, 0, 1] }, Hair: { roughness: 0.4, alphaCutoff: 0.5 } };
    expect(effectiveValue(o, 'Draw', { slot: 'Hair' }, 'roughness', imported)).toEqual({ value: 0.1, source: 'slot' });
    expect(effectiveValue(o, 'Draw', { slot: 'Body' }, 'roughness', imported)).toEqual({ value: 0.9, source: 'object' });
    expect(effectiveValue(o, 'Draw', { slot: 'Body' }, 'baseColor', imported)).toEqual({ value: [1, 0, 0, 1], source: 'imported' });
    expect(effectiveValue(o, 'Draw', { slot: 'Hair' }, 'alphaCutoff', imported)).toEqual({ value: undefined, source: 'default' });
    expect(effectiveValue(o, 'Draw', {}, 'metallic', imported)).toEqual({ value: undefined, source: 'default' });
  });

  it('既定値と数への変換', () => {
    expect(defaultNumbers('baseColor', 'float', 4)).toEqual([1, 1, 1, 1]);
    expect(defaultNumbers('roughness', 'float', 1)).toEqual([0]);
    expect(toNumbers([1, 0.5], 4)).toEqual([1, 0.5, 0, 0]);
    expect(toNumbers(0.25, 1)).toEqual([0.25]);
    expect(toNumbers('asset:x', 1)).toBeNull();
  });
});
