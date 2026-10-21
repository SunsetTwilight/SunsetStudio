// ============================================================
//  保存していない変更の確認 (ウィンドウを閉じる / プロジェクトを閉じる・開くとき)
//
//  「保存して続ける」「保存しない」「キャンセル」。答えは state/commands.ts の answerClosePrompt に渡す。
//  ビューポート (ネイティブの子ウィンドウ) に重なるので data-viewport-hole で切り抜く。
// ============================================================
import { useEffect } from 'react';
import { answerClosePrompt } from '../state/commands';
import { useStudio } from '../state/store';

export function UnsavedDialog() {
  const prompt = useStudio((s) => s.closePrompt);
  const unsaved = useStudio((s) => s.unsaved);
  const sceneDirty = useStudio((s) => s.scene?.dirty ?? false);

  useEffect(() => {
    if (!prompt) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') void answerClosePrompt('cancel');
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [prompt]);

  if (!prompt) return null;
  const items: string[] = [];
  if (unsaved?.graph) items.push('レンダーグラフ (ノードエディター)');
  for (const p of unsaved?.pipelines ?? []) items.push(`パイプライン: ${p}`);
  if (unsaved?.scene || sceneDirty) items.push('シーン');
  const what = prompt === 'window' ? 'ウィンドウを閉じる' : 'プロジェクトを閉じる';

  return (
    <div className="modal-backdrop">
      <div className="modal unsaved-dialog" data-viewport-hole>
        <div className="modal-title">保存していない変更があります</div>
        <p>{what}前に保存しますか？</p>
        <ul className="unsaved-list">
          {items.map((x) => (
            <li key={x}>{x}</li>
          ))}
        </ul>
        <div className="modal-actions">
          <button onClick={() => void answerClosePrompt('cancel')}>キャンセル</button>
          <button onClick={() => void answerClosePrompt('discard')}>保存しない</button>
          <button className="primary" autoFocus onClick={() => void answerClosePrompt('save')}>
            保存して続ける
          </button>
        </div>
      </div>
    </div>
  );
}
