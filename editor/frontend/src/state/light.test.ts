// ============================================================
//  ライトの編集の関数のテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import type { LightTypeInfo, SceneJson } from '../bridge/protocol';
import { lightParam, lightShadow, linkCandidates, newLight, withLightLinks, withLightParam, withLightShadow, withLightType } from './light';
import { newObject } from './scene';

const point: LightTypeInfo = {
  id: 'point',
  label: 'ポイント',
  shape: 'point',
  params: [
    { name: 'color', label: '色', type: 'color', default: [1, 1, 1] },
    { name: 'intensity', label: '強さ', type: 'float', default: 10, min: 0 },
  ],
};
const spot: LightTypeInfo = { ...point, id: 'spot', label: 'スポット', shape: 'spot', params: [...point.params, { name: 'innerAngle', label: '内側', type: 'float', default: 20 }] };
const sun: LightTypeInfo = { id: 'directional', label: '平行光源', shape: 'directional', params: point.params };

describe('ライト', () => {
  const base: SceneJson = { version: 1, objects: [{ ...newObject({ version: 1, objects: [] }), id: 'hero' }] };

  it('ライトの物体を作る', () => {
    const l = newLight(base, point);
    expect(l.id).toBe('point-1');
    expect(l.model).toBe('');
    expect(l.light).toEqual({ type: 'point', params: {} });
    expect(newLight(base, sun).rotation).toEqual([-50, 30, 0]);
  });

  it('値: 書いていなければ既定値、書けば上書き', () => {
    let s: SceneJson = { ...base, objects: [...base.objects, newLight(base, point)] };
    const o = () => s.objects[1];
    expect(lightParam(o(), point, 'intensity')).toEqual([10]);
    expect(lightParam(o(), point, 'color')).toEqual([1, 1, 1]);
    s = withLightParam(s, 'point-1', 'intensity', [25]);
    s = withLightParam(s, 'point-1', 'color', [1, 0, 0]);
    expect(o().light?.params).toEqual({ intensity: 25, color: [1, 0, 0] });
    expect(lightParam(o(), point, 'color')).toEqual([1, 0, 0]);
  });

  it('種類を替えると、新しい種類にない値は外す', () => {
    let s: SceneJson = { ...base, objects: [...base.objects, newLight(base, spot)] };
    s = withLightParam(s, 'spot-1', 'innerAngle', [5]);
    s = withLightParam(s, 'spot-1', 'intensity', [3]);
    s = withLightType(s, 'spot-1', point);
    expect(s.objects[1].light).toEqual({ type: 'point', params: { intensity: 3 } });
  });

  it('ライトリンク: 照らす物体 (空なら書かない)、選べるのはライト以外', () => {
    let s: SceneJson = { ...base, objects: [...base.objects, newLight(base, point)] };
    s = withLightLinks(s, 'point-1', ['hero']);
    expect(s.objects[1].light?.links).toEqual(['hero']);
    s = withLightLinks(s, 'point-1', []);
    expect(s.objects[1].light && 'links' in s.objects[1].light).toBe(false);
    expect(linkCandidates(s).map((o) => o.id)).toEqual(['hero']);
  });

  it('影: 書いていなければ既定値、変えた所だけ上書き', () => {
    let s: SceneJson = { ...base, objects: [...base.objects, newLight(base, sun)] };
    expect(lightShadow(s.objects[1])).toEqual({ enabled: false, bias: 0.001, normalBias: 0.03, softness: 1.5 });
    s = withLightShadow(s, 'directional-1', { enabled: true });
    s = withLightShadow(s, 'directional-1', { softness: 3 });
    expect(s.objects[1].light?.shadow).toEqual({ enabled: true, bias: 0.001, normalBias: 0.03, softness: 3 });
  });
});
