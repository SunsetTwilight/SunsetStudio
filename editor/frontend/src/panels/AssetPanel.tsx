// ============================================================
//  ファイルを編集するタブ (M4): シェーダー (.hlsl) とパイプラインの JSON を Monaco で編集する
//
//    Ctrl+S / 「保存してコンパイル」  asset.write で保存する。描画スレッドのホットリロードが反映する
//                                      (コンパイルに失敗したら、前のシェーダーのまま描く)。
//                                      シェーダーは保存した内容を shader.compile で確かめ、エラーの行に印を付ける
//    「コンパイルだけ」                保存せずに、今の内容でコンパイルして確かめる
//    「VS Code で開く」「既定のアプリ」 外部のエディターで開く。外で保存した変更は、ここにも読み直す
//  タブの id は "asset:<アセットのパス>"。保存していない変更があると、タイトルに ● を付ける。
// ============================================================
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { IDockviewPanelProps } from 'dockview-react';
import { call, isConnected, on } from '../bridge/client';
import type { ShaderCompileResult, ShaderUsage } from '../bridge/protocol';
import { CodeEditor, type CodeEditorApi, type EditorMarker } from '../editor/CodeEditor';
import { parseShaderMessages, sameShaderFile, type ShaderDiagnostic } from '../editor/hlsl';
import { languageOf } from '../editor/monaco';
import { fileName, loadPipelineShaders, type Stage } from '../state/assets';
import { openPipelineTab } from '../state/dock';
import { errorText } from '../state/graph';
import { studioStore, useStudio } from '../state/store';

export interface AssetParams {
  path: string;
}

type Status = { kind: 'idle' | 'busy' | 'ok' | 'error'; text: string };

export function AssetPanel(props: IDockviewPanelProps<AssetParams>) {
  const { path } = props.params;
  const language = languageOf(path);
  const isShader = language === 'hlsl';
  const [initial, setInitial] = useState<string | null>(null);
  const [error, setError] = useState('');
  const saved = useRef('');
  const current = useRef('');
  const editor = useRef<CodeEditorApi | null>(null);
  const [dirty, setDirty] = useState(false);
  const [external, setExternal] = useState(false); // 保存していない変更があるのに、外で変更された
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });
  const [result, setResult] = useState<ShaderCompileResult | null>(null);
  const [usages, setUsages] = useState<ShaderUsage[]>([]);
  const reloadError = useStudio((s) => s.shaderErrors[path]);
  const graph = useStudio((s) => s.graph);

  // 読み込み
  useEffect(() => {
    if (!isConnected()) return;
    call('asset.read', { path })
      .then((r) => {
        saved.current = current.current = r.text;
        setInitial(r.text);
      })
      .catch((e) => setError(errorText(e)));
  }, [path]);

  // 使っているパイプライン (ステージとエントリー)
  useEffect(() => {
    if (!isShader || !isConnected()) return;
    call('shader.usages', { path })
      .then((r) => setUsages(r.usages))
      .catch(() => setUsages([]));
  }, [path, isShader, graph?.revision]);

  // タイトル (保存していない変更は ●)
  useEffect(() => {
    props.api.setTitle(`${fileName(path)}${dirty ? ' ●' : ''}`);
  }, [dirty, path, props.api]);

  // 外で保存された変更を読み直す (2 秒ごと。保存していない変更があれば知らせるだけ)
  useEffect(() => {
    if (initial === null || !isConnected()) return;
    const t = window.setInterval(() => {
      call('asset.read', { path })
        .then((r) => {
          if (r.text === saved.current) return;
          if (current.current === saved.current) {
            saved.current = current.current = r.text;
            editor.current?.setValue(r.text);
            setDirty(false);
            setStatus({ kind: 'ok', text: '外部の変更を読み直しました' });
          } else setExternal(true);
        })
        .catch(() => {});
    }, 2000);
    return () => window.clearInterval(t);
  }, [initial, path]);

  const stage: Stage | undefined = usages[0]?.stage;
  const compile = useCallback(
    async (text: string, label: string) => {
      if (!isShader) return;
      setStatus({ kind: 'busy', text: `${label}…` });
      try {
        const u = usages[0];
        const r = await call('shader.compile', { path, text, ...(u ? { stage: u.stage, entry: u.entry, profile: u.profile || undefined } : {}) });
        setResult(r);
        const diags = parseShaderMessages(r.messages);
        const errs = diags.filter((d) => d.severity === 'error').length;
        const warns = diags.filter((d) => d.severity === 'warning').length;
        setStatus(
          r.ok
            ? { kind: 'ok', text: `${label}: コンパイルできました${warns ? ` (警告 ${warns})` : ''}` }
            : { kind: 'error', text: `${label}: コンパイルできません (エラー ${errs || 1})${label.startsWith('保存') ? '。描画は前のシェーダーのまま' : ''}` },
        );
      } catch (e) {
        setStatus({ kind: 'error', text: errorText(e) });
      }
    },
    [isShader, path, usages],
  );

  const save = useCallback(async () => {
    const text = current.current;
    if (language === 'json') {
      try {
        JSON.parse(text);
      } catch (e) {
        return setStatus({ kind: 'error', text: `JSON の形が正しくありません: ${String(e)}` });
      }
    }
    try {
      setStatus({ kind: 'busy', text: '保存しています…' });
      await call('asset.write', { path, text });
      saved.current = text;
      setDirty(current.current !== saved.current);
      setExternal(false);
      if (isShader) await compile(text, '保存しました');
      else {
        setStatus({ kind: 'ok', text: '保存しました (パイプラインは自動で読み直します)' });
        if (path.startsWith('pipelines/')) void loadPipelineShaders(studioStore.get().graph?.pipelines ?? []);
      }
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  }, [path, language, isShader, compile]);

  const reloadFromDisk = useCallback(async () => {
    const r = await call('asset.read', { path });
    saved.current = current.current = r.text;
    editor.current?.setValue(r.text);
    setDirty(false);
    setExternal(false);
    setStatus({ kind: 'ok', text: '読み直しました' });
  }, [path]);

  const openExternal = useCallback(
    async (app: 'vscode' | 'default') => {
      try {
        await call('asset.openExternal', { path, app });
        setStatus({ kind: 'ok', text: app === 'vscode' ? 'VS Code で開きました (保存するとここにも反映します)' : '既定のアプリで開きました' });
      } catch (e) {
        setStatus({ kind: 'error', text: errorText(e) });
      }
    },
    [path],
  );

  // エラー / 警告: コンパイルの結果と、ホットリロードの失敗 (保存したファイル)
  const diagnostics: ShaderDiagnostic[] = useMemo(() => {
    if (result) return parseShaderMessages(result.messages);
    if (reloadError) return parseShaderMessages(reloadError);
    return [];
  }, [result, reloadError]);
  const markers: EditorMarker[] = useMemo(
    () => diagnostics.filter((d) => sameShaderFile(d.file, path)).map((d) => ({ line: d.line, column: d.column, severity: d.severity, message: d.message })),
    [diagnostics, path],
  );

  // ホットリロードの結果が届いたら、表示を合わせる
  useEffect(
    () =>
      on('shader.status', (e) => {
        if (e.path !== path) return;
        setStatus(e.ok ? { kind: 'ok', text: '描画に反映しました' } : { kind: 'error', text: 'コンパイルできません。描画は前のシェーダーのまま' });
        if (e.ok) setResult((r) => (r && !r.ok ? null : r));
      }),
    [path],
  );

  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;
  if (error) return <div className="panel"><div className="inspector-status error">{error}</div></div>;
  if (initial === null) return <div className="panel"><div className="placeholder">読み込んでいます…</div></div>;

  return (
    <div className="panel asset-panel">
      <header className="panel-header">
        <span className="panel-title" title={path}>{path}</span>
        {isShader && <span className="muted">{stage ? `${stage}${usages[0]?.profile ? ` · ${usages[0].profile}` : ''} · ${usages[0]?.entry ?? 'main'}` : '(使っているパイプラインなし)'}</span>}
        <span className="spacer" />
        <button onClick={() => void save()} title="保存して描画に反映する (Ctrl+S)">
          {isShader ? '保存してコンパイル' : '保存'}
        </button>
        {isShader && (
          <button onClick={() => void compile(current.current, 'コンパイルだけ')} title="保存せずに、今の内容でコンパイルして確かめる">
            コンパイルだけ
          </button>
        )}
        <button onClick={() => void openExternal('vscode')} title="VS Code で開く (code コマンド)">VS Code</button>
        <button onClick={() => void openExternal('default')} title="OS の既定のアプリで開く">既定のアプリ</button>
      </header>
      {isShader && usages.length > 0 && (
        <div className="asset-usages">
          使っているパイプライン:
          {usages.map((u) => (
            <button key={u.pipeline + u.stage} className="link" onClick={() => openPipelineTab(u.pipeline)}>
              {fileName(u.pipeline)} ({u.stage})
            </button>
          ))}
        </div>
      )}
      {external && (
        <div className="inspector-status busy">
          外部でファイルが変更されました (ここにも保存していない変更があります)
          <button onClick={() => void reloadFromDisk()}>外部の内容を読み直す</button>
        </div>
      )}
      {status.text && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}
      <div className="asset-editor">
        <CodeEditor
          value={initial}
          language={language}
          markers={markers}
          api={(a) => (editor.current = a)}
          onChange={(t) => {
            current.current = t;
            setDirty(t !== saved.current);
          }}
          onSave={() => void save()}
        />
      </div>
      {diagnostics.length > 0 && (
        <ul className="asset-diagnostics">
          {diagnostics.map((d, i) => {
            const here = sameShaderFile(d.file, path);
            return (
              <li key={i} className={d.severity}>
                <button className="link" disabled={!here} onClick={() => editor.current?.revealLine(d.line, d.column)} title={d.file}>
                  {here ? '' : `${fileName(d.file)} `}
                  {d.line}:{d.column}
                </button>
                <span className="sev">{d.severity === 'error' ? 'エラー' : d.severity === 'warning' ? '警告' : '注記'}</span>
                {d.message}
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}
