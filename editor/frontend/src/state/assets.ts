// ============================================================
//  シェーダー / パイプラインのファイルの状態 (M4 のシェーダー編集)
//
//    loadPipelineShaders   パイプラインのアセット (JSON) を読み、どのシェーダーを使うかを覚える (ノードの表示、右クリック)
//    pipelineShaderFiles   パイプラインの JSON のテキストから、ステージごとのファイルを取り出す
//    withShaderFile        パイプラインの JSON のテキストの、ステージのファイルを差し替える (書式は保たない)
//    shaderErrorsOf        パイプラインのシェーダーで、コンパイルに失敗しているもの
// ============================================================
import { call, isConnected } from '../bridge/client';
import type { JsonObject } from '../bridge/protocol';
import { studioStore, type PipelineShaders } from './store';

export type Stage = 'vs' | 'ps' | 'cs';
export const STAGES: Stage[] = ['vs', 'ps', 'cs'];

export function pipelineShaderFiles(text: string): PipelineShaders {
  try {
    const j = JSON.parse(text) as JsonObject;
    const shaders = (j.shaders ?? {}) as Record<string, { file?: string }>;
    const out: PipelineShaders = {};
    for (const s of STAGES) if (shaders[s]?.file) out[s] = String(shaders[s].file);
    return out;
  } catch {
    return {};
  }
}

export function withShaderFile(text: string, stage: Stage, file: string): string {
  const j = JSON.parse(text) as JsonObject;
  const shaders = { ...((j.shaders ?? {}) as Record<string, JsonObject>) };
  shaders[stage] = { ...(shaders[stage] ?? {}), file };
  return JSON.stringify({ ...j, shaders }, null, 2) + '\n';
}

export async function loadPipelineShaders(ids: string[]): Promise<void> {
  if (!isConnected()) return;
  const next: Record<string, PipelineShaders> = {};
  await Promise.all(
    ids.map(async (id) => {
      try {
        next[id] = pipelineShaderFiles((await call('asset.read', { path: id })).text);
      } catch {
        /* 読めないパイプラインは出さない */
      }
    }),
  );
  studioStore.setPipelineShaders(next);
}

export async function loadShaderErrors(): Promise<void> {
  if (!isConnected()) return;
  try {
    const r = await call('shader.status');
    studioStore.setShaderErrors(Object.fromEntries(r.errors.map((e) => [e.path, e.text])));
  } catch {
    /* プロジェクトがない */
  }
}

export function shaderErrorsOf(pipeline: string, shaders: Record<string, PipelineShaders>, errors: Record<string, string>): string[] {
  const s = shaders[pipeline];
  if (!s) return [];
  return STAGES.map((st) => s[st]).filter((f): f is string => !!f && f in errors);
}

// ファイル名だけ ("shaders/lighting.ps.hlsl" → "lighting.ps.hlsl")
export function fileName(path: string): string {
  const i = path.lastIndexOf('/');
  return i >= 0 ? path.slice(i + 1) : path;
}
