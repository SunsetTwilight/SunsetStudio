// ============================================================
//  マテリアルの編集の関数のテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import type { LibraryInfo, MaterialJson, MaterialVar, SceneJson, SceneObjectJson } from '../bridge/protocol';
import { autoMaterialKey, isColorParam, newMaterialPath, objectSlots, paramValue, paramValues, withAssignment, withParam, withPipeline, withTexture } from './material';

const ID = '0123456789abcdef0123456789abcdef';
const obj = (id: string, model = 'builtin:cube'): SceneObjectJson => ({
  id,
  name: id,
  model,
  parent: null,
  position: [0, 0, 0],
  rotation: [0, 0, 0],
  scale: [1, 1, 1],
  visible: true,
  color: [1, 1, 1, 1],
});
const lib: LibraryInfo = {
  extensions: [],
  assets: [
    {
      id: ID,
      path: 'hero/hero.fbx',
      importer: 'model',
      external: false,
      missing: false,
      cached: true,
      model: {
        status: 'ready',
        info: { nodes: 1, meshes: 1, vertices: 3, triangles: 1, slots: 2, skins: 0, morphs: 0, boundsMin: [0, 0, 0], boundsMax: [1, 1, 1], loadMs: 1, slotNames: ['Body', 'Hair'] },
      },
    },
  ],
};
const v = (name: string, columns: number, type: MaterialVar['type'] = 'float', elements = 0): MaterialVar => ({ name, type, rows: 1, columns, elements, offset: 0 });
const mat: MaterialJson = { version: 1, passes: { Opaque: 'pipelines/a.json' }, params: { baseColor: [1, 0.5, 0.25, 1], roughness: 0.5 }, textures: {} };

describe('マテリアル', () => {
  it('スロットと、取り込んだ値のキー', () => {
    expect(objectSlots(obj('c'), lib)).toEqual(['Default']);
    expect(objectSlots(obj('m', `asset:${ID}`), lib)).toEqual(['Body', 'Hair']);
    expect(objectSlots(obj('x', 'asset:ffffffffffffffffffffffffffffffff'), lib)).toEqual([]);
    expect(autoMaterialKey(obj('c'), 0)).toBe('auto:cube');
    expect(autoMaterialKey(obj('m', `asset:${ID}`), 1)).toBe(`auto:${ID}:1`);
  });

  it('割り当てと外す (空になれば materials を消す)', () => {
    const scene: SceneJson = { version: 1, objects: [obj('a'), obj('b')] };
    const s1 = withAssignment(scene, 'a', 'Default', 'materials/red.material.json');
    expect(s1.objects[0].materials).toEqual({ Default: 'materials/red.material.json' });
    expect(s1.objects[1].materials).toBeUndefined();
    const s2 = withAssignment(s1, 'a', 'Default', null);
    expect(s2.objects[0].materials).toBeUndefined();
  });

  it('新しいファイルの名前は重ならない', () => {
    expect(newMaterialPath('Hero', 'Body', [])).toBe('materials/hero-body.material.json');
    expect(newMaterialPath('Hero', 'Body', ['materials/hero-body.material.json'])).toBe('materials/hero-body-2.material.json');
    expect(newMaterialPath('キャラ', '体', [])).toBe('materials/model-model.material.json');
  });

  it('値の取り出しと書き込み', () => {
    expect(paramValues(mat, v('baseColor', 4))).toEqual([1, 0.5, 0.25, 1]);
    expect(paramValues(mat, v('roughness', 1))).toEqual([0.5]);
    expect(paramValues(mat, v('metallic', 1))).toEqual([0]);
    expect(paramValues(mat, v('tint', 3, 'float', 2))).toHaveLength(6);
    expect(paramValue(v('roughness', 1), [0.25])).toBe(0.25);
    expect(paramValue(v('flag', 1, 'bool'), [1])).toBe(true);
    expect(paramValue(v('baseColor', 4), [1, 1, 1, 1])).toEqual([1, 1, 1, 1]);
    expect(withParam(mat, 'roughness', 0.9).params.roughness).toBe(0.9);
    expect(mat.params.roughness).toBe(0.5); // 元は変えない
    expect(withTexture(mat, 'baseColorMap', `asset:${ID}`).textures.baseColorMap).toBe(`asset:${ID}`);
    expect(withTexture(withTexture(mat, 'n', 'asset:x'), 'n', null).textures).toEqual({});
    expect(withPipeline(mat, 'Opaque', 'pipelines/b.json').passes.Opaque).toBe('pipelines/b.json');
  });

  it('色の欄にする変数', () => {
    expect(isColorParam(v('baseColor', 4))).toBe(true);
    expect(isColorParam(v('emissiveColor', 3))).toBe(true);
    expect(isColorParam(v('roughness', 1))).toBe(false);
    expect(isColorParam(v('colorScale', 1))).toBe(false);
    expect(isColorParam(v('tint', 3))).toBe(false);
  });
});
