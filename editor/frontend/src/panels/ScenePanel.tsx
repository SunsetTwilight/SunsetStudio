// ============================================================
//  シーンのパネル (M5-2): ビューポートのシーンの物体を木で並べる
//
//    一覧   親子の木 (▸ で畳む)、目のボタンで表示 / 非表示、クリックで選ぶ (インスペクターで配置を編集。
//           ビューポートには辺の印)。ビューポートをクリックしても選べる
//    操作   ＋立方体 (選んでいる物と同じ親に加える)、複製、削除 (Delete キーでも)、保存 (● は保存していない変更)
//  編集はシーン全体の JSON を scene.set で送る (元に戻す / やり直すが効く)。保存は scene.save。
// ============================================================
import { useEffect, useState } from 'react';
import { call, isConnected } from '../bridge/client';
import { errorText } from '../state/graph';
import { assetName, assetOfModel } from '../state/library';
import { addObject, applyScene, duplicateObject, loadScene, newObject, removeObject, sceneTree, toHex, updateObject } from '../state/scene';
import { studioStore, useStudio } from '../state/store';

type Status = { kind: 'idle' | 'busy' | 'ok' | 'error'; text: string };

export function ScenePanel() {
  const info = useStudio((s) => s.scene);
  const library = useStudio((s) => s.library);
  const modelLabel = (model: string) => {
    const a = assetOfModel(library, model);
    return a ? assetName(a.path) : model.startsWith('asset:') ? '(ないモデル)' : model.replace(/^builtin:/, '');
  };
  const selection = useStudio((s) => s.selection);
  const [collapsed, setCollapsed] = useState<Set<string>>(new Set());
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });
  const selectedId = selection?.kind === 'object' ? selection.id : '';

  useEffect(() => {
    if (isConnected() && !info) void loadScene();
  }, [info]);

  const apply = async (label: string, next: Parameters<typeof applyScene>[0]) => {
    const err = await applyScene(next, label);
    setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: label });
    return !err;
  };

  // ビューポートのクリックなどで選んだ行を、一覧の見える位置に出す (畳んだ親の中なら開く)
  useEffect(() => {
    if (!selectedId || !info) return;
    const ancestors = new Set<string>();
    let cur = info.scene.objects.find((o) => o.id === selectedId)?.parent ?? null;
    while (cur && !ancestors.has(cur)) {
      ancestors.add(cur);
      cur = info.scene.objects.find((o) => o.id === cur)?.parent ?? null;
    }
    if ([...ancestors].some((a) => collapsed.has(a))) setCollapsed(new Set([...collapsed].filter((c) => !ancestors.has(c))));
    requestAnimationFrame(() => document.querySelector('.scene-row.selected')?.scrollIntoView({ block: 'nearest' }));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedId]);

  // Delete キーで選んでいる物を消す (文字の入力欄では効かせない)
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Delete' || !selectedId || !info) return;
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
      if (!t?.closest('.scene-panel')) return;
      e.preventDefault();
      void remove();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });

  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;
  if (!info) return <div className="panel"><div className="placeholder">シーンを読み込んでいます…</div></div>;
  const scene = info.scene;
  const rows = sceneTree(scene, collapsed);
  const selected = scene.objects.find((o) => o.id === selectedId);

  const add = async () => {
    const obj = newObject(scene);
    if (selected) obj.parent = selected.parent;
    if (await apply(`${obj.id} を追加`, addObject(scene, obj))) studioStore.select({ kind: 'object', id: obj.id });
  };
  const duplicate = async () => {
    if (!selected) return;
    const r = duplicateObject(scene, selected.id);
    if (r && (await apply(`${selected.name} を複製`, r.scene))) studioStore.select({ kind: 'object', id: r.id });
  };
  async function remove() {
    if (!selected) return;
    if (await apply(`${selected.name} を削除`, removeObject(scene, selected.id))) studioStore.select(null);
  }
  const save = async () => {
    try {
      await call('scene.save');
      const cur = studioStore.get().scene;
      if (cur) studioStore.setScene({ ...cur, dirty: false });
      setStatus({ kind: 'ok', text: `保存しました (${info.file})` });
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  };

  return (
    <div className="panel scene-panel" tabIndex={-1}>
      <header className="panel-header">
        <span className="panel-title" title={info.file}>
          {info.file}
          {info.dirty ? ' ●' : ''}
        </span>
        <span className="muted">{scene.objects.length} 個</span>
        <span className="spacer" />
        <button onClick={() => void add()} title="立方体を加える (選んでいる物と同じ親に)">＋ 立方体</button>
        <button onClick={() => void duplicate()} disabled={!selected} title="選んでいる物を複製する">複製</button>
        <button onClick={() => void remove()} disabled={!selected} title="選んでいる物を消す (Delete)。子は親につなぎ直す">削除</button>
        <button onClick={() => void save()} title="シーンのファイルに書く">保存</button>
      </header>
      {info.error && <div className="inspector-status error">{info.error}</div>}
      {status.text && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}
      <div className="scene-body">
        {rows.map(({ obj, depth, hasChildren }) => (
          <div
            key={obj.id}
            className={`scene-row${obj.id === selectedId ? ' selected' : ''}${obj.visible ? '' : ' hidden-object'}`}
            style={{ paddingLeft: 6 + depth * 14 }}
            onClick={() => studioStore.select({ kind: 'object', id: obj.id })}
          >
            <button
              className="scene-fold"
              disabled={!hasChildren}
              onClick={(e) => {
                e.stopPropagation();
                const next = new Set(collapsed);
                if (next.has(obj.id)) next.delete(obj.id);
                else next.add(obj.id);
                setCollapsed(next);
              }}
            >
              {hasChildren ? (collapsed.has(obj.id) ? '▸' : '▾') : ''}
            </button>
            <span className="scene-swatch" style={{ background: toHex(obj.color) }} />
            <span className="scene-name">{obj.name || obj.id}</span>
            <span className="scene-model muted">{modelLabel(obj.model)}</span>
            <button
              className="scene-eye"
              title={obj.visible ? '表示中 (クリックで隠す)' : '非表示 (クリックで表示)'}
              onClick={(e) => {
                e.stopPropagation();
                void apply(`${obj.name} を${obj.visible ? '隠す' : '表示'}`, updateObject(scene, obj.id, { visible: !obj.visible }));
              }}
            >
              {obj.visible ? '◉' : '○'}
            </button>
          </div>
        ))}
        {rows.length === 0 && <div className="placeholder">物体がありません (＋ 立方体で加える)</div>}
      </div>
    </div>
  );
}
