// ============================================================
//  起動画面 (M4a): プロジェクトを開いていないときに出す
//
//    最近のプロジェクト  クリックで開く (なくなったものは灰色)
//    新規作成            名前、場所、テンプレートを選んで作り、そのまま開く
//    開く…               OS のダイアログで .sunsetproj を選ぶ
// ============================================================
import { useEffect, useState } from 'react';
import { call, isConnected } from '../bridge/client';
import type { ProjectTemplate, RecentProject } from '../bridge/protocol';
import { errorText } from '../state/graph';
import { studioStore, useStudio } from '../state/store';

type Status = { kind: 'idle' | 'busy' | 'error'; text: string };

export function StartScreen() {
  const engine = useStudio((s) => s.engine);
  const [recent, setRecent] = useState<RecentProject[]>([]);
  const [templates, setTemplates] = useState<ProjectTemplate[]>([]);
  const [name, setName] = useState('MyProject');
  const [location, setLocation] = useState('');
  const [template, setTemplate] = useState('deferred');
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });

  useEffect(() => {
    if (!isConnected()) return;
    call('project.recent')
      .then((r) => {
        setRecent(r.projects);
        setLocation((cur) => cur || r.defaultLocation);
      })
      .catch((e) => setStatus({ kind: 'error', text: errorText(e) }));
    call('project.templates')
      .then((t) => {
        setTemplates(t);
        setTemplate((cur) => (t.some((x) => x.id === cur) ? cur : (t[0]?.id ?? '')));
      })
      .catch(() => {});
  }, []);

  const busy = status.kind === 'busy';
  const engineReady = engine?.status === 'ready';

  const run = async (text: string, action: () => Promise<void>) => {
    setStatus({ kind: 'busy', text });
    try {
      await action();
      setStatus({ kind: 'idle', text: '' });
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  };

  const open = (file: string) =>
    run('プロジェクトを開いています…', async () => {
      studioStore.setProject(await call('project.open', { file }));
    });

  const openDialog = async () => {
    const r = await call('dialog.openProject');
    if (r.file) await open(r.file);
  };

  const pickFolder = async () => {
    const r = await call('dialog.pickFolder', { initial: location });
    if (r.folder) setLocation(r.folder);
  };

  const create = () =>
    run('プロジェクトを作っています…', async () => {
      studioStore.setProject(await call('project.create', { location, name: name.trim(), template }));
    });

  return (
    <div className="start-screen">
      <div className="start-inner">
        <h1 className="start-title">SunsetEditor</h1>
        {!engineReady && (
          <div className={`inspector-status ${engine?.status === 'failed' ? 'error' : 'busy'}`}>
            {engine?.status === 'failed' ? `エンジンを初期化できませんでした: ${engine.error ?? ''}` : 'エンジンを初期化しています…'}
          </div>
        )}
        {status.text && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}

        <div className="start-columns">
          <section className="start-section">
            <div className="start-heading">
              最近のプロジェクト
              <span className="spacer" />
              <button onClick={() => void openDialog()} disabled={busy || !engineReady}>
                開く…
              </button>
            </div>
            {recent.length === 0 && <div className="muted start-empty">まだありません。右で新しいプロジェクトを作るか、「開く…」で選んでください。</div>}
            {recent.map((p) => (
              <button
                key={p.file}
                className={`start-recent${p.exists ? '' : ' missing'}`}
                disabled={busy || !engineReady || !p.exists}
                title={p.exists ? p.file : `見つかりません: ${p.file}`}
                onClick={() => void open(p.file)}
              >
                <span className="start-recent-name">{p.name}</span>
                <span className="start-recent-path">{p.file}</span>
              </button>
            ))}
          </section>

          <section className="start-section">
            <div className="start-heading">新規作成</div>
            <div className="form-row">
              <span className="form-label">名前</span>
              <span className="form-control">
                <input value={name} onChange={(e) => setName(e.target.value)} />
              </span>
            </div>
            <div className="form-row">
              <span className="form-label">場所</span>
              <span className="form-control">
                <input value={location} onChange={(e) => setLocation(e.target.value)} />
                <button onClick={() => void pickFolder()} disabled={busy}>
                  参照…
                </button>
              </span>
            </div>
            <div className="form-row">
              <span className="form-label">テンプレート</span>
              <span className="form-control">
                <select value={template} onChange={(e) => setTemplate(e.target.value)}>
                  {templates.map((t) => (
                    <option key={t.id} value={t.id}>
                      {t.label}
                    </option>
                  ))}
                </select>
              </span>
            </div>
            <div className="muted start-hint">
              {location && name.trim() ? `${location}\\${name.trim()}\\${name.trim()}.sunsetproj を作ります` : ''}
            </div>
            <div className="start-actions">
              <button className="primary" onClick={() => void create()} disabled={busy || !engineReady || !name.trim() || !location || !template}>
                作成して開く
              </button>
            </div>
          </section>
        </div>
      </div>
    </div>
  );
}
