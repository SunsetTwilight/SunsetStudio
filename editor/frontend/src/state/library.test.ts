// ============================================================
//  アセットのライブラリの関数のテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import type { LibraryAsset, LibraryInfo, SceneJson } from '../bridge/protocol';
import { assetName, assetOfModel, idPrefix, modelObject, modelRef } from './library';

const asset = (id: string, path: string, extra: Partial<LibraryAsset> = {}): LibraryAsset => ({
  id,
  path,
  importer: 'model',
  external: false,
  missing: false,
  cached: true,
  ...extra,
});

describe('アセットのライブラリ', () => {
  it('名前と id の接頭辞', () => {
    expect(assetName('chars/hero.fbx')).toBe('hero');
    expect(assetName('box/box.tar.gz')).toBe('box');
    expect(idPrefix('Hero Robot')).toBe('hero-robot');
    expect(idPrefix('キャラ')).toBe('model');
  });

  it('モデルを置く物体: 名前、参照、底を地面に、重ならない id', () => {
    const a = asset('0123456789abcdef0123456789abcdef', 'hero/hero.fbx', {
      model: {
        status: 'ready',
        info: { nodes: 1, meshes: 1, vertices: 3, triangles: 1, slots: 1, skins: 0, morphs: 0, boundsMin: [-1, -0.5, -1], boundsMax: [1, 2, 1], loadMs: 1 },
      },
    });
    const scene: SceneJson = { version: 1, objects: [] };
    const o = modelObject(scene, a);
    expect(o.id).toBe('hero-1');
    expect(o.name).toBe('hero');
    expect(o.model).toBe('asset:0123456789abcdef0123456789abcdef');
    expect(o.position).toEqual([0, 0.5, 0]);
    expect(o.color).toEqual([1, 1, 1, 1]);
    expect(modelObject({ version: 1, objects: [o] }, a).id).toBe('hero-2');
    // 読んでいないモデルは原点に
    expect(modelObject(scene, asset('ab'.repeat(16), 'x.obj')).position).toEqual([0, 0, 0]);
  });

  it('物体の model からアセットを探す', () => {
    const a = asset('ab'.repeat(16), 'x.obj');
    const lib: LibraryInfo = { assets: [a], extensions: ['.obj'] };
    expect(assetOfModel(lib, modelRef(a))).toBe(a);
    expect(assetOfModel(lib, 'builtin:cube')).toBeUndefined();
    expect(assetOfModel(null, modelRef(a))).toBeUndefined();
  });
});
