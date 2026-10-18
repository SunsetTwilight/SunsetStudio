// ============================================================
//  パイプラインのタブ (M4): パイプラインのアセットと、シェーダーとのつながり
//
//    シェーダー     ステージ (VS / PS / CS) ごとのファイル。開く (シェーダーのタブ)、差し替える、新しく作って差し替える
//    リフレクション シェーダーが使うリソース (t0 gAlbedo …)、定数バッファ、入力 / 出力のセマンティクス
//    ステート       ブレンド / ラスタライザー / 深度などは、インスペクターのフォームで編集する (「ステートを編集」)
//  差し替えはパイプラインの JSON を書き換えて保存する。描画スレッドのホットリロードが反映する。
//  タブの id は "pipeline:<アセットのパス>"。
// ============================================================
import { useCallback, useEffect, useState } from 'react';
import type { IDockviewPanelProps } from 'dockview-react';
import { call, isConnected } from '../bridge/client';
import type { JsonObject, ShaderCompileResult } from '../bridge/protocol';
import { parseShaderMessages } from '../editor/hlsl';
import { fileName, loadPipelineShaders, pipelineShaderFiles, STAGES, withShaderFile, type Stage } from '../state/assets';
import { openAssetTab, showPanel } from '../state/dock';
import { errorText } from '../state/graph';
import { studioStore, useStudio, type PipelineShaders } from '../state/store';
import { NewShaderDialog } from './NewShaderDialog';

export interface PipelineParams {
  id: string;
}

const STAGE_LABEL: Record<Stage, string> = { vs: 'VS (頂点)', ps: 'PS (ピクセル)', cs: 'CS (Compute)' };
const REGISTER: Record<string, string> = {
  CBuffer: 'b', TBuffer: 't', Texture: 't', Sampler: 's', Structured: 't', ByteAddress: 't', RTAccelerationStructure: 't',
};
function registerOf(b: JsonObject): string {
  const type = String(b.type ?? '');
  const letter = REGISTER[type] ?? (type.startsWith('UAV') ? 'u' : '?');
  const space = Number(b.space ?? 0);
  return `${letter}${String(b.bindPoint ?? 0)}${space ? `, space${space}` : ''}`;
}

type Status = { kind: 'idle' | 'busy' | 'ok' | 'error'; text: string };

export function PipelinePanel(props: IDockviewPanelProps<PipelineParams>) {
  const { id } = props.params;
  const graph = useStudio((s) => s.graph);
  const errors = useStudio((s) => s.shaderErrors);
  const [text, setText] = useState<string | null>(null);
  const [shaders, setShaders] = useState<PipelineShaders>({});
  const [files, setFiles] = useState<string[]>([]);
  const [compiled, setCompiled] = useState<Partial<Record<Stage, ShaderCompileResult>>>({});
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });
  const [creating, setCreating] = useState<Stage | null>(null);

  const load = useCallback(async () => {
    try {
      const r = await call('asset.read', { path: id });
      setText(r.text);
      const s = pipelineShaderFiles(r.text);
      setShaders(s);
      setFiles((await call('asset.list', { dir: 'shaders', extensions: ['.hlsl'] })).files);
      // 各ステージをコンパイルだけして、リフレクションとエラーを出す
      const out: Partial<Record<Stage, ShaderCompileResult>> = {};
      for (const st of STAGES) {
        const f = s[st];
        if (!f) continue;
        try {
          out[st] = await call('shader.compile', { path: f, stage: st });
        } catch (e) {
          out[st] = { path: f, stage: st, ok: false, messages: errorText(e) };
        }
      }
      setCompiled(out);
    } catch (e) {
      setStatus({ kind: 'error', text: errorText(e) });
    }
  }, [id]);

  useEffect(() => {
    if (isConnected()) void load();
  }, [load]);

  // ファイルを保存したあと (シェーダーのエラーの状態が変わった) に読み直す
  useEffect(() => {
    if (text !== null) void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [errors]);

  const replace = useCallback(
    async (stage: Stage, file: string) => {
      if (!text || !file) return;
      try {
        setStatus({ kind: 'busy', text: '差し替えています…' });
        await call('asset.write', { path: id, text: withShaderFile(text, stage, file) });
        setStatus({ kind: 'ok', text: `${STAGE_LABEL[stage]} を ${fileName(file)} に差し替えました` });
        await load();
        void loadPipelineShaders(studioStore.get().graph?.pipelines ?? []);
      } catch (e) {
        setStatus({ kind: 'error', text: errorText(e) });
      }
    },
    [id, text, load],
  );

  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;
  if (text === null) return <div className="panel">{status.text ? <div className="inspector-status error">{status.text}</div> : <div className="placeholder">読み込んでいます…</div>}</div>;

  const passes = (graph?.graph.passes as JsonObject[] | undefined)?.filter((p) => p.pipeline === id).map((p) => String(p.name)) ?? [];
  const used = STAGES.filter((st) => shaders[st]);

  return (
    <div className="panel pipeline-panel">
      <header className="panel-header">
        <span className="panel-title" title={id}>{fileName(id)}</span>
        <span className="muted">{passes.length ? `パス: ${passes.join(', ')}` : '(使っているパスなし)'}</span>
        <span className="spacer" />
        <button
          onClick={() => {
            studioStore.select({ kind: 'pipeline', id });
            showPanel('inspector');
          }}
          title="ブレンド、ラスタライザー、深度などをインスペクターで編集する"
        >
          ステートを編集
        </button>
        <button onClick={() => openAssetTab(id)} title="パイプラインの JSON をそのまま編集する">JSON を開く</button>
        <button onClick={() => void load()}>読み直す</button>
      </header>
      {status.text && <div className={`inspector-status ${status.kind}`}>{status.text}</div>}
      <div className="pipeline-body">
        {used.map((st) => {
          const f = shaders[st]!;
          const c = compiled[st];
          const diags = c ? parseShaderMessages(c.messages) : [];
          const refl = c?.reflection;
          return (
            <section key={st} className="pipeline-stage">
              <div className="pipeline-stage-head">
                <span className="stage-label">{STAGE_LABEL[st]}</span>
                <button className="link" onClick={() => openAssetTab(f)} title="シェーダーのタブで開く">{f}</button>
                {(errors[f] || (c && !c.ok)) && <span className="badge error">エラー {Math.max(1, diags.filter((d) => d.severity === 'error').length)}</span>}
                <span className="spacer" />
                <select
                  value=""
                  title="このステージのシェーダーを差し替える"
                  onChange={(e) => e.target.value && void replace(st, e.target.value)}
                >
                  <option value="">差し替える…</option>
                  {files
                    .filter((x) => x !== f && (!/\.(vs|ps|cs)\./i.test(x) || new RegExp(`\\.${st}\\.`, 'i').test(x)))
                    .map((x) => (
                      <option key={x} value={x}>{x}</option>
                    ))}
                </select>
                <button onClick={() => setCreating(st)} title="テンプレートから新しく作って、このステージに使う">新しく作る</button>
              </div>
              {refl && (
                <div className="pipeline-reflection">
                  <div>
                    <span className="muted">リソース</span>
                    {refl.bindings.length === 0 ? ' なし' : refl.bindings.map((b) => (
                      <span key={`${String(b.name)}${registerOf(b)}`} className="refl-item" title={`${String(b.type)} ${String(b.dimension)}`}>
                        {registerOf(b)} <b>{String(b.name)}</b>
                      </span>
                    ))}
                  </div>
                  {refl.cbuffers.length > 0 && (
                    <div>
                      <span className="muted">定数バッファ</span>
                      {refl.cbuffers.map((cb) => (
                        <span key={String(cb.name)} className="refl-item">
                          <b>{String(cb.name)}</b> {String(cb.size)} B
                        </span>
                      ))}
                    </div>
                  )}
                  {st !== 'cs' && (
                    <div>
                      <span className="muted">{st === 'ps' ? '出力' : '入力'}</span>
                      {(st === 'ps' ? refl.outputs : refl.inputs).map((s, i) => (
                        <span key={i} className="refl-item">
                          {String(s.semanticName)}{Number(s.semanticIndex) || ''}
                        </span>
                      ))}
                    </div>
                  )}
                  {st === 'cs' && <div><span className="muted">numthreads</span> <span className="refl-item">{refl.threadGroupSize.join(', ')}</span></div>}
                </div>
              )}
              {c && !c.ok && <pre className="pipeline-errors">{c.messages || 'コンパイルできません'}</pre>}
            </section>
          );
        })}
        {used.length === 0 && <div className="placeholder">シェーダーがありません</div>}
      </div>
      {creating && (
        <NewShaderDialog
          stage={creating}
          defaultName={`${fileName(id).replace(/\.json$/, '')}_custom`}
          title={`新しい ${STAGE_LABEL[creating]} を作る`}
          note={`作ったシェーダーを、このパイプライン (${fileName(id)}) の ${STAGE_LABEL[creating]} にする`}
          onCreated={async (path) => {
            await replace(creating, path);
            openAssetTab(path);
          }}
          onClose={() => setCreating(null)}
        />
      )}
    </div>
  );
}
