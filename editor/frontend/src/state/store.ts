// ============================================================
//  エディターの状態 (C++ から届いたイベントの結果)。
//  パネルは dockview の中で別々に描かれるので、props ではなくここから読む (useStudio)。
// ============================================================
import { useSyncExternalStore } from 'react';
import type { AppInfo, EditState, EngineInfo, LibraryInfo, FrameStats, GraphInfo, LogEntry, PreviewInfo, ProjectInfo, SceneInfo, ViewerPixel, ViewerSettings } from '../bridge/protocol';

// インスペクターに表示するもの (グラフのパネルで選ぶ)
export type Selection =
  | { kind: 'pass'; name: string }
  | { kind: 'resource'; name: string }
  | { kind: 'pipeline'; id: string }
  | { kind: 'object'; id: string }; // シーンの物体 (M5-2)

export interface StudioState {
  info: AppInfo | null;
  engine: EngineInfo | null;
  stats: FrameStats | null;
  logs: LogEntry[];
  connError: string | null;
  graph: GraphInfo | null;
  selection: Selection | null;
  project: ProjectInfo | null; // null = まだ問い合わせていない
  edit: EditState | null; // 編集の履歴の状態
  viewer: ViewerSettings | null; // リソースビューアーの設定
  viewerPixel: ViewerPixel | null;
  previews: PreviewInfo[]; // 別ウィンドウのプレビュー
  viewportFloating: boolean; // ビューポートを別ウィンドウにしている
  shaderErrors: Record<string, string>; // コンパイルに失敗しているシェーダー (アセットのパス → メッセージ)
  pipelineShaders: Record<string, PipelineShaders>; // パイプラインのアセット → シェーダーのファイル
  scene: SceneInfo | null; // ビューポートのシーン (M5-2)
  library: LibraryInfo | null; // アセットのライブラリ (M5-3)
  windowMaximized: boolean; // メインのウィンドウが最大化しているか (タイトルバーのボタン)
  commandError: string; // メニューの操作の失敗 (タイトルバーに出す)
  materialFiles: string[]; // materials/*.material.json (M5-4)
}

export interface PipelineShaders {
  vs?: string;
  ps?: string;
  cs?: string;
}

const MAX_LOG_ENTRIES = 5000;

let state: StudioState = { info: null, engine: null, stats: null, logs: [], connError: null, graph: null, selection: null, project: null, edit: null, viewer: null, viewerPixel: null, previews: [], viewportFloating: false, shaderErrors: {}, pipelineShaders: {}, scene: null, library: null, windowMaximized: false, commandError: '', materialFiles: [] };
const listeners = new Set<() => void>();

function set(patch: Partial<StudioState>): void {
  state = { ...state, ...patch };
  for (const fn of listeners) fn();
}

export const studioStore = {
  get: () => state,
  subscribe(fn: () => void): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
  },
  setInfo: (info: AppInfo) => set({ info, engine: state.engine ?? info.engine, windowMaximized: info.window?.maximized ?? false }),
  setEngine: (engine: EngineInfo) => set({ engine }),
  setStats: (stats: FrameStats) => set({ stats }),
  setConnError: (connError: string | null) => set({ connError }),
  addLog(entry: LogEntry) {
    const logs = state.logs.length >= MAX_LOG_ENTRIES ? state.logs.slice(state.logs.length - MAX_LOG_ENTRIES + 1) : state.logs.slice();
    logs.push(entry);
    set({ logs });
  },
  clearLogs: () => set({ logs: [] }),
  setGraph: (graph: GraphInfo) => set({ graph }),
  select: (selection: Selection | null) => set({ selection }),
  setEdit: (edit: EditState) => set({ edit }),
  setViewer: (viewer: ViewerSettings) => set({ viewer }),
  setViewerPixel: (viewerPixel: ViewerPixel) => set({ viewerPixel }),
  setPreviews: (previews: PreviewInfo[]) => set({ previews }),
  setViewportFloating: (viewportFloating: boolean) => set({ viewportFloating }),
  setShaderErrors: (shaderErrors: Record<string, string>) => set({ shaderErrors }),
  setShaderStatus(path: string, ok: boolean, text: string) {
    const next = { ...state.shaderErrors };
    if (ok) delete next[path];
    else next[path] = text;
    set({ shaderErrors: next });
  },
  setPipelineShaders: (pipelineShaders: Record<string, PipelineShaders>) => set({ pipelineShaders }),
  setScene: (scene: SceneInfo | null) => set({ scene }),
  setLibrary: (library: LibraryInfo | null) => set({ library }),
  setWindowMaximized: (windowMaximized: boolean) => set({ windowMaximized }),
  setCommandError: (commandError: string) => set({ commandError }),
  setMaterialFiles: (materialFiles: string[]) => set({ materialFiles }),
  // プロジェクトが変わったら、前のプロジェクトのグラフと選択と統計は捨てる
  setProject: (project: ProjectInfo) =>
    set(
      state.project?.file === project.file && state.project?.open === project.open
        ? { project }
        : { project, graph: null, selection: null, stats: null, viewer: null, viewerPixel: null, shaderErrors: {}, pipelineShaders: {}, scene: null, library: null, materialFiles: [] },
    ),
};

export function useStudio<T>(select: (s: StudioState) => T): T {
  return useSyncExternalStore(studioStore.subscribe, () => select(studioStore.get()));
}
