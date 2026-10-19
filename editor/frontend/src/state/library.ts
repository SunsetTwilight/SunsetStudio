// ============================================================
//  アセットのライブラリの状態 (M5-3): プロジェクトの assets/ のモデルとテクスチャ
//
//    loadLibrary     library.list を読み、ストアに入れる (library.changed のたびに呼ぶ)
//    ensureModel     モデルを読んでもらい、読み終わるまで待つ (シーンに置く前に境界を知るため)
//    modelObject     モデルをシーンに置く物体を作る (名前はファイル名、地面の上に置く)
//    assetOfModel    物体の model ("asset:<ID>") のアセット
//    assetKindLabel  種類の表示名
// ============================================================
import { call, isConnected } from '../bridge/client';
import type { LibraryAsset, LibraryInfo, SceneJson, SceneObjectJson } from '../bridge/protocol';
import { uniqueId } from './scene';
import { studioStore } from './store';

export async function loadLibrary(): Promise<LibraryInfo | null> {
  if (!isConnected()) return null;
  try {
    const info = await call('library.list');
    studioStore.setLibrary(info);
    return info;
  } catch {
    studioStore.setLibrary(null); // プロジェクトがない
    return null;
  }
}

// モデルを読んでもらい、読み終わった (か失敗した) アセットを返す。時間切れなら最後の状態
export async function ensureModel(id: string, timeoutMs = 15000): Promise<LibraryAsset | undefined> {
  const find = () => studioStore.get().library?.assets.find((a) => a.id === id);
  const done = () => {
    const s = find()?.model?.status;
    return s === 'ready' || s === 'error';
  };
  if (done()) return find();
  await call('library.load', { id });
  const t0 = Date.now();
  while (!done() && Date.now() - t0 < timeoutMs) {
    await new Promise((r) => setTimeout(r, 100));
    if (!done()) await loadLibrary();
  }
  return find();
}

export const ASSET_MODEL_PREFIX = 'asset:';

export function modelRef(asset: LibraryAsset): string {
  return ASSET_MODEL_PREFIX + asset.id;
}

export function assetOfModel(library: LibraryInfo | null, model: string): LibraryAsset | undefined {
  if (!library || !model.startsWith(ASSET_MODEL_PREFIX)) return undefined;
  const id = model.slice(ASSET_MODEL_PREFIX.length);
  return library.assets.find((a) => a.id === id);
}

// "chars/hero.fbx" → "hero"
export function assetName(path: string): string {
  const file = path.slice(path.lastIndexOf('/') + 1);
  const dot = file.indexOf('.');
  return dot > 0 ? file.slice(0, dot) : file;
}

// 物体の id の接頭辞 (英数字と - _ だけ。使えなければ "model")
export function idPrefix(name: string): string {
  const s = name
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 40);
  return s || 'model';
}

// モデルを置く物体。モデルの境界がわかっていれば、底を地面 (y = 0) に合わせる
export function modelObject(scene: SceneJson, asset: LibraryAsset, parent: string | null = null): SceneObjectJson {
  const name = assetName(asset.path);
  const minY = asset.model?.info?.boundsMin[1];
  return {
    id: uniqueId(scene, idPrefix(name)),
    name,
    model: modelRef(asset),
    parent,
    position: [0, minY !== undefined && Number.isFinite(minY) ? -minY : 0, 0],
    rotation: [0, 0, 0],
    scale: [1, 1, 1],
    visible: true,
    color: [1, 1, 1, 1], // モデルはスロットの基本色を使う (物体の色を掛ける)
    layer: 'Opaque',
  };
}

export function assetKindLabel(importer: string): string {
  return importer === 'model' ? 'モデル' : importer === 'texture' ? 'テクスチャ' : importer;
}
