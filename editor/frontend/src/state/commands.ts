// ============================================================
//  メニューから呼ぶ操作 (タイトルバー兼メニューバー)
//
//  失敗はログのパネルに出し、タイトルバーにも短く出す (studioStore.setCommandError)。
//  ツールバーにあった操作 (プロジェクトを閉じる、元に戻す / やり直す、ビューポートの切り離し …) もここに集めた。
// ============================================================
import { call, isConnected } from '../bridge/client';
import type { RecentProject } from '../bridge/protocol';
import { showPanel } from './dock';
import { errorText } from './graph';
import { loadLibrary } from './library';
import { studioStore } from './store';

export function reportError(e: unknown) {
  const text = errorText(e);
  studioStore.addLog({ timeMs: performance.now(), level: 'error', source: 'ui', id: 0, text });
  studioStore.setCommandError(text);
}

async function run(fn: () => Promise<void>) {
  try {
    studioStore.setCommandError('');
    await fn();
  } catch (e) {
    reportError(e);
  }
}

// ---- プロジェクト ----
export const closeProject = () =>
  run(async () => {
    studioStore.setProject(await call('project.close'));
  });

// 開いているプロジェクトがあれば閉じてから開く (別のプロジェクトのパネルやプレビューを残さない)
async function openFile(file: string) {
  if (studioStore.get().project?.open) studioStore.setProject(await call('project.close'));
  studioStore.setProject(await call('project.open', { file }));
}

export const openProjectDialog = () =>
  run(async () => {
    const r = await call('dialog.openProject');
    if (r.file) await openFile(r.file);
  });

export const openRecent = (p: RecentProject) => run(() => openFile(p.file));

// 新しいプロジェクトは起動画面で作る (テンプレート、場所、名前を選ぶ)
export const newProject = closeProject;

export async function recentProjects(): Promise<RecentProject[]> {
  if (!isConnected()) return [];
  try {
    return (await call('project.recent')).projects;
  } catch {
    return [];
  }
}

export const saveScene = () =>
  run(async () => {
    await call('scene.save');
    const cur = studioStore.get().scene;
    if (cur) studioStore.setScene({ ...cur, dirty: false });
  });

export const importAsset = (mode: 'copy' | 'link') =>
  run(async () => {
    const r = await call('library.import', { mode });
    if (r.canceled) return;
    await loadLibrary();
    showPanel('library');
  });

// 古い構成のプロジェクトを、直下に並べる構成に移す (ファイルを動かすので確認する)
export const migrateProject = () =>
  run(async () => {
    const ok = window.confirm(
      'Assets/ の中身 (graphs/ pipelines/ shaders/ など) をプロジェクトの直下に移し、Assets/ を消します。\n' +
        'プロジェクトを開き直すので、元に戻す / やり直すの履歴は消えます。\n' +
        '直下に同じ名前のフォルダがあれば、何も動かさずに中止します。\n\n移しますか？',
    );
    if (!ok) return;
    studioStore.setProject(await call('project.migrate'));
  });

// ---- 編集 ----
export const undoRedo = (undo: boolean) =>
  run(async () => {
    const r = await call(undo ? 'edit.undo' : 'edit.redo');
    studioStore.setEdit(r.state);
  });

// ---- ウィンドウ ----
export const floatViewport = (floating: boolean) =>
  run(async () => {
    const r = await call('viewport.float', { floating });
    studioStore.setViewportFloating(r.floating);
  });

export const minimizeWindow = () => run(async () => void (await call('window.minimize')));
export const toggleMaximize = () =>
  run(async () => {
    const r = await call('window.toggleMaximize');
    studioStore.setWindowMaximized(r.maximized);
  });
export const closeWindow = () => run(async () => void (await call('window.close')));
export const openDevTools = () => run(async () => void (await call('window.devTools')));

// ---- ヘルプ ----
export async function ping(): Promise<string> {
  const t0 = performance.now();
  try {
    await call('app.ping', { t: t0 });
    return `${(performance.now() - t0).toFixed(1)} ms`;
  } catch (e) {
    return errorText(e);
  }
}
