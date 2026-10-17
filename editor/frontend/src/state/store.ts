// ============================================================
//  エディターの状態 (C++ から届いたイベントの結果)。
//  パネルは dockview の中で別々に描かれるので、props ではなくここから読む (useStudio)。
// ============================================================
import { useSyncExternalStore } from 'react';
import type { AppInfo, EngineInfo, FrameStats, GraphInfo, LogEntry, ProjectInfo } from '../bridge/protocol';

// インスペクターに表示するもの (グラフのパネルで選ぶ)
export type Selection =
  | { kind: 'pass'; name: string }
  | { kind: 'resource'; name: string }
  | { kind: 'pipeline'; id: string };

export interface StudioState {
  info: AppInfo | null;
  engine: EngineInfo | null;
  stats: FrameStats | null;
  logs: LogEntry[];
  connError: string | null;
  graph: GraphInfo | null;
  selection: Selection | null;
  project: ProjectInfo | null; // null = まだ問い合わせていない
}

const MAX_LOG_ENTRIES = 5000;

let state: StudioState = { info: null, engine: null, stats: null, logs: [], connError: null, graph: null, selection: null, project: null };
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
  setInfo: (info: AppInfo) => set({ info, engine: state.engine ?? info.engine }),
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
  // プロジェクトが変わったら、前のプロジェクトのグラフと選択と統計は捨てる
  setProject: (project: ProjectInfo) =>
    set(
      state.project?.file === project.file && state.project?.open === project.open
        ? { project }
        : { project, graph: null, selection: null, stats: null },
    ),
};

export function useStudio<T>(select: (s: StudioState) => T): T {
  return useSyncExternalStore(studioStore.subscribe, () => select(studioStore.get()));
}
