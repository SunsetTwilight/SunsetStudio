// ============================================================
//  ライブラリのパネル (M5-3): プロジェクトの assets/ のモデルとテクスチャ (SunsetAsset)
//
//    取り込む…     ファイルを選んで assets/ にコピーする (モデルは assets/<名前>/ に、参照しているテクスチャも一緒に)
//    外部を参照…   コピーせず、外部のファイルを参照する .meta だけを assets/ に作る
//    更新          assets/ を走査し直し、元のファイルが変わったモデルを読み直す
//    シーンに置く  モデルを物体としてシーンに加える (選んでいる物と同じ親に。底を地面に合わせる)
//    取り込み直す  キャッシュ (Cache/assets) を消して取り込み直す
//  行を選ぶと、下にモデルの情報 (ノード、メッシュ、三角形、スロット …) を出す (モデルはそのときに読む)。
// ============================================================
import { useEffect, useState } from 'react';
import { call, isConnected } from '../bridge/client';
import type { LibraryAsset } from '../bridge/protocol';
import { errorText } from '../state/graph';
import { assetKindLabel, ensureModel, loadLibrary, modelObject } from '../state/library';
import { addObject, applyScene, loadScene } from '../state/scene';
import { studioStore, useStudio } from '../state/store';

type Status = { kind: 'idle' | 'busy' | 'ok' | 'error'; text: string };

export function LibraryPanel() {
  const library = useStudio((s) => s.library);
  const selection = useStudio((s) => s.selection);
  const [selectedId, setSelectedId] = useState('');
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });

  useEffect(() => {
    if (isConnected() && !library) void loadLibrary();
  }, [library]);

  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;
  if (!library) return <div className="panel"><div className="placeholder">ライブラリを読み込んでいます…</div></div>;

  const importFile = async (mode: 'copy' | 'link') => {
    setStatus({ kind: 'busy', text: mode === 'copy' ? '取り込んでいます…' : '参照を作っています…' });
    try {
      const r = await call('library.import', { mode });
      if (r.canceled) return setStatus({ kind: 'idle', text: '' });
      await loadLibrary();
      if (r.id) setSelectedId(r.id);
      const extra = r.copied?.length ? ` (ほかに ${r.copied.length} ファイルをコピー)` : '';
      const warn = r.warnings?.length ? ` / 警告 ${r.warnings.length} (ログ)` : '';
      setStatus({ kind: r.warnings?.length ? 'busy' : 'ok', text: `${mode === 'copy' ? '取り込みました' : '参照しました'}: ${r.path}${extra}${warn}` });
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  };

  const refresh = async () => {
    try {
      const r = await call('library.refresh');
      await loadLibrary();
      const parts = [r.created ? `新しいファイル ${r.created}` : '', r.reloaded.length ? `読み直し ${r.reloaded.length}` : ''].filter(Boolean);
      setStatus({ kind: 'ok', text: `更新しました${parts.length ? ` (${parts.join('、')})` : ''}` });
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  };

  const place = async (target: LibraryAsset) => {
    // 境界を知るために先に読む (底を地面に合わせる)。読めなくても置く (原点に)
    setStatus({ kind: 'busy', text: `${target.path} を読み込んでいます…` });
    const asset = (await ensureModel(target.id).catch(() => undefined)) ?? target;
    if (!studioStore.get().scene) await loadScene();
    const scene = studioStore.get().scene;
    if (!scene) return setStatus({ kind: 'error', text: 'シーンを読めません' });
    const selected = selection?.kind === 'object' ? scene.scene.objects.find((o) => o.id === selection.id) : undefined;
    const obj = modelObject(scene.scene, asset, selected?.parent ?? null);
    const err = await applyScene(addObject(scene.scene, obj), `${obj.name} を置く`);
    if (err) return setStatus({ kind: 'error', text: err });
    studioStore.select({ kind: 'object', id: obj.id });
    setStatus({ kind: 'ok', text: `シーンに置きました: ${obj.name} (${obj.id})` });
  };

  const reimport = async (asset: LibraryAsset) => {
    try {
      await call('library.reimport', { id: asset.id });
      await loadLibrary();
      setStatus({ kind: 'ok', text: `取り込み直しています: ${asset.path}` });
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  };

  const selected = library.assets.find((a) => a.id === selectedId);

  return (
    <div className="panel library-panel">
      <header className="panel-header">
        <span className="panel-title">assets/</span>
        <span className="muted">{library.assets.length} 個</span>
        <span className="spacer" />
        <button onClick={() => void importFile('copy')} title={`ファイルを assets/ にコピーして取り込む (${library.extensions.join(' ')})`}>
          取り込む…
        </button>
        <button onClick={() => void importFile('link')} title="コピーせず、外部のファイルを参照する (.meta だけを assets/ に作る)">
          外部を参照…
        </button>
        <button onClick={() => void refresh()} title="assets/ を読み直し、元のファイルが変わったモデルを取り込み直す">
          更新
        </button>
      </header>
      {status.text && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}
      <div className="library-body">
        {library.assets.map((a) => (
          <div
            key={a.id}
            className={`library-row${a.id === selectedId ? ' selected' : ''}${a.missing ? ' missing' : ''}`}
            onClick={() => {
              setSelectedId(a.id);
              if (a.importer === 'model' && !a.missing && !a.model) void call('library.load', { id: a.id }).catch(() => {});
            }}
            onDoubleClick={() => a.importer === 'model' && !a.missing && void place(a)}
            title={a.external ? `外部: ${a.source}` : a.path}
          >
            <span className={`library-kind ${a.importer}`}>{assetKindLabel(a.importer)}</span>
            <span className="library-path">{a.path}</span>
            {a.external && <span className="badge">外部</span>}
            {a.missing && <span className="badge error">見つからない</span>}
            {a.model?.status === 'loading' && <span className="badge">読み込み中</span>}
            {a.model?.status === 'error' && <span className="badge error" title={a.model.error}>エラー</span>}
            {a.importer === 'model' && !a.missing && (
              <button
                className="library-place"
                onClick={(e) => {
                  e.stopPropagation();
                  void place(a);
                }}
                title="シーンに置く (ダブルクリックでも)"
              >
                置く
              </button>
            )}
          </div>
        ))}
        {library.assets.length === 0 && <div className="placeholder">アセットがありません (「取り込む…」で FBX / glTF / OBJ / PMX / PMD / 画像を加える)</div>}
      </div>
      {selected && <AssetDetails asset={selected} onReimport={() => void reimport(selected)} />}
    </div>
  );
}

function AssetDetails({ asset, onReimport }: { asset: LibraryAsset; onReimport: () => void }) {
  const info = asset.model?.info;
  const fmt = (v: number[]) => v.map((x) => Math.round(x * 1000) / 1000).join(', ');
  return (
    <div className="library-details">
      <div className="form-row">
        <span className="form-label">ID</span>
        <span className="form-control muted selectable">{asset.id}</span>
      </div>
      {asset.external && (
        <div className="form-row">
          <span className="form-label">外部</span>
          <span className="form-control muted selectable">{asset.source}</span>
        </div>
      )}
      <div className="form-row">
        <span className="form-label">キャッシュ</span>
        <span className="form-control">
          <span className={asset.cached ? '' : 'muted'}>{asset.cached ? '新しい' : 'まだ / 古い (使うときに作る)'}</span>
          <button onClick={onReimport} disabled={asset.missing} title="キャッシュを消して取り込み直す">取り込み直す</button>
        </span>
      </div>
      {asset.model?.status === 'error' && <div className="inspector-status error">{asset.model.error}</div>}
      {info && (
        <>
          <div className="form-row">
            <span className="form-label">形</span>
            <span className="form-control muted">
              ノード {info.nodes}、メッシュ {info.meshes}、頂点 {info.vertices.toLocaleString()}、三角形 {info.triangles.toLocaleString()}
            </span>
          </div>
          <div className="form-row">
            <span className="form-label">ほか</span>
            <span className="form-control muted">
              スロット {info.slots}、スキン {info.skins}、モーフ {info.morphs} (読み込み {info.loadMs} ms)
            </span>
          </div>
          <div className="form-row">
            <span className="form-label">境界 (m)</span>
            <span className="form-control muted">
              [{fmt(info.boundsMin)}] 〜 [{fmt(info.boundsMax)}]
            </span>
          </div>
        </>
      )}
      {asset.importer === 'model' && !info && asset.model?.status !== 'error' && (
        <div className="muted library-hint">読み込んでいます…</div>
      )}
    </div>
  );
}
