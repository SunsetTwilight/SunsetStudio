// ============================================================
//  シーンの編集の関数のテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import type { SceneJson, SceneObjectJson } from '../bridge/protocol';
import { addObject, descendants, duplicateObject, fromHex, newObject, parentCandidates, removeObject, sceneTree, toHex, updateObject } from './scene';

const o = (id: string, parent: string | null = null): SceneObjectJson => ({ ...newObject({ version: 1, objects: [] }), id, name: id, parent });
const scene: SceneJson = { version: 1, objects: [o('a'), o('b', 'a'), o('c', 'b'), o('d')] };

describe('シーンの編集', () => {
  it('木: 親の順に、深さ付きで並べ、畳んだ子は出さない', () => {
    expect(sceneTree(scene).map((r) => `${r.depth}:${r.obj.id}`)).toEqual(['0:a', '1:b', '2:c', '0:d']);
    expect(sceneTree(scene, new Set(['a'])).map((r) => r.obj.id)).toEqual(['a', 'd']);
    expect(sceneTree(scene)[0].hasChildren).toBe(true);
  });

  it('親にできるのは自分と子孫以外', () => {
    expect([...descendants(scene, 'a')].sort()).toEqual(['b', 'c']);
    expect(parentCandidates(scene, 'a').map((x) => x.id)).toEqual(['d']);
    expect(parentCandidates(scene, 'c').map((x) => x.id)).toEqual(['a', 'b', 'd']);
  });

  it('消すと、子は消した物の親につなぎ直す', () => {
    const s = removeObject(scene, 'b');
    expect(s.objects.map((x) => x.id)).toEqual(['a', 'c', 'd']);
    expect(s.objects.find((x) => x.id === 'c')!.parent).toBe('a');
  });

  it('複製・追加・更新', () => {
    const d = duplicateObject({ version: 1, objects: [o('cube-1')] }, 'cube-1')!;
    expect(d.id).toBe('cube-2');
    expect(d.scene.objects[1].position[0]).toBe(1);
    const n = newObject(scene);
    expect(n.id).toBe('cube-1');
    expect(addObject(scene, n).objects).toHaveLength(5);
    expect(updateObject(scene, 'd', { name: 'D' }).objects[3].name).toBe('D');
  });

  it('色の変換', () => {
    expect(toHex([1, 0.5, 0])).toBe('#ff8000');
    expect(fromHex('#ff8000', 0.5)).toEqual([1, 128 / 255, 0, 0.5]);
  });
});
