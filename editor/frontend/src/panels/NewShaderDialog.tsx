// ============================================================
//  新しいシェーダーを作るダイアログ (テンプレートから)
//
//  作ったら onCreated にアセットのパスを渡す。パスにつなぐときは、呼び出し側がパイプラインを複製して差し替える。
// ============================================================
import { useEffect, useState } from 'react';
import { call } from '../bridge/client';
import type { AssetTemplate } from '../bridge/protocol';
import type { Stage } from '../state/assets';
import { errorText } from '../state/graph';

interface Props {
  stage: Stage; // 作るシェーダーのステージ (テンプレートを絞る)
  defaultName: string; // 拡張子なしの名前
  title: string;
  note?: string; // 作ったあとに起きること (パイプラインの複製など)
  onCreated: (path: string) => void | Promise<void>;
  onClose: () => void;
}

export function NewShaderDialog({ stage, defaultName, title, note, onCreated, onClose }: Props) {
  const [templates, setTemplates] = useState<AssetTemplate[]>([]);
  const [template, setTemplate] = useState('');
  const [name, setName] = useState(defaultName);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    call('asset.templates')
      .then((r) => {
        const list = r.templates.filter((t) => t.kind === 'shader' && t.stage === stage);
        setTemplates(list);
        setTemplate(list[0]?.id ?? '');
      })
      .catch((e) => setError(errorText(e)));
  }, [stage]);

  const t = templates.find((x) => x.id === template);
  const path = t ? `${t.folder}/${name.trim()}${t.suffix}` : '';
  const valid = /^[A-Za-z0-9_\-]+$/.test(name.trim());

  const create = async () => {
    if (!t) return setError('テンプレートを選んでください');
    if (!valid) return setError('名前は英数字、_、- で入力してください');
    setBusy(true);
    try {
      setError('');
      await call('asset.create', { template: t.id, path });
      await onCreated(path);
      onClose();
    } catch (e) {
      setError(errorText(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal" onClick={(e) => e.stopPropagation()}>
        <div className="modal-title">{title}</div>
        <label className="modal-row">
          テンプレート
          <select value={template} onChange={(e) => setTemplate(e.target.value)}>
            {templates.map((x) => (
              <option key={x.id} value={x.id}>
                {x.label}
              </option>
            ))}
          </select>
        </label>
        {t && <div className="muted modal-note">{t.description}</div>}
        <label className="modal-row">
          名前
          <input value={name} autoFocus onChange={(e) => setName(e.target.value)} onKeyDown={(e) => e.key === 'Enter' && void create()} />
        </label>
        <div className="muted modal-note">作るファイル: {path || '—'}</div>
        {note && <div className="muted modal-note">{note}</div>}
        {error && <div className="inspector-status error">{error}</div>}
        <div className="modal-actions">
          <button onClick={onClose}>やめる</button>
          <button onClick={() => void create()} disabled={busy || !t}>
            作る
          </button>
        </div>
      </div>
    </div>
  );
}
