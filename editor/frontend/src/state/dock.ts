// ============================================================
//  パネルの配置 (dockview) の操作。ツールバーやノードエディターから使う
//
//    showPanel      パネルを前に出す (閉じていたら開く)
//    popoutPanel    パネルを別ウィンドウにする。Web のパネルは dockview の popout (WebView2 の window.open)、
//                   ビューポートはネイティブの子ウィンドウなので、C++ が別ウィンドウに移す (viewport.float)
//    watchDragOut   タブをウィンドウの外へドラッグして離したら、そのパネルを別ウィンドウにする
//    openPreviewTab リソースのプレビューをタブ (パネル) で開く。外へ出すとネイティブの別ウィンドウにする
//    openAssetTab   シェーダー / パイプラインの JSON をタブで編集する (Monaco)
//    openPipelineTab パイプラインのタブ (シェーダーとのつながり、リフレクション)
//    defaultLayout  既定の配置 / resetLayout 既定に戻す
// ============================================================
import type { DockviewApi } from 'dockview-react';
import { call, isConnected } from '../bridge/client';
import { errorText } from './graph';
import { studioStore } from './store';

let dockApi: DockviewApi | null = null;

export function setDockApi(api: DockviewApi | null) {
  dockApi = api;
  patchWindowOpen(); // 保存した配置の popout を開き直すときにも要る
}

export const PANEL_TITLES: Record<string, string> = {
  viewport: 'ビューポート',
  graph: 'レンダーグラフ',
  inspector: 'インスペクター',
  log: 'ログ',
  viewer: 'リソースビューアー',
  nodes: 'ノードエディター',
  scene: 'シーン',
  library: 'ライブラリ',
};

// 既定のレイアウト
export function defaultLayout(api: DockviewApi) {
  api.addPanel({ id: 'viewport', component: 'viewport', title: 'ビューポート' });
  api.addPanel({
    id: 'graph',
    component: 'graph',
    title: 'レンダーグラフ',
    position: { referencePanel: 'viewport', direction: 'left' },
    initialWidth: 300,
  });
  api.addPanel({
    id: 'scene',
    component: 'scene',
    title: 'シーン',
    position: { referencePanel: 'graph', direction: 'within' },
  });
  api.addPanel({
    id: 'library',
    component: 'library',
    title: 'ライブラリ',
    position: { referencePanel: 'graph', direction: 'within' },
  });
  api.addPanel({
    id: 'inspector',
    component: 'inspector',
    title: 'インスペクター',
    position: { referencePanel: 'viewport', direction: 'right' },
    initialWidth: 380,
  });
  api.addPanel({
    id: 'nodes',
    component: 'nodes',
    title: 'ノードエディター',
    position: { referencePanel: 'viewport', direction: 'below' },
    initialHeight: 380,
  });
  api.addPanel({
    id: 'log',
    component: 'log',
    title: 'ログ',
    position: { referencePanel: 'nodes', direction: 'within' },
  });
  api.addPanel({
    id: 'viewer',
    component: 'viewer',
    title: 'リソースビューアー',
    position: { referencePanel: 'nodes', direction: 'within' },
  });
  api.getPanel('nodes')?.api.setActive();
  api.getPanel('viewport')?.api.setActive();
}

// パネルの配置を既定に戻す (メニューの「表示 > 配置を元に戻す」)。別ウィンドウ (popout) のパネルも戻す
export function resetLayout() {
  if (!dockApi) return;
  dockApi.clear();
  defaultLayout(dockApi);
}

// パネルがなければ開く (保存した配置から閉じたパネルを戻す)
export function showPanel(id: string) {
  if (!dockApi) return;
  const existing = dockApi.getPanel(id);
  if (existing) {
    existing.api.setActive();
    return;
  }
  // シーンはレンダーグラフと同じ場所 (左)、ほかはノードエディターの場所
  const ref =
    (id === 'scene' || id === 'library' ? dockApi.getPanel('graph') : undefined) ?? dockApi.getPanel('nodes') ?? dockApi.getPanel('log') ?? dockApi.getPanel('viewport');
  dockApi.addPanel({
    id,
    component: id,
    title: PANEL_TITLES[id] ?? id,
    position: ref ? { referencePanel: ref.id, direction: 'within' } : undefined,
  });
}

// popout の窓に読み込むページ。配布時 (仮想ホスト) と開発サーバーは public/popout.html (同じオリジン)。
// 仮想ホストを使えない古いランタイムで webview_set_html を使ったときは about:blank
function popoutUrl(): string {
  return location.protocol === 'about:' ? 'about:blank' : '/popout.html';
}

// about:blank の窓は window.open の中で読み込みが終わり、dockview が待つ load が来ないことがある。
// 開いた直後に load が来ていなければ、こちらで load を送る (dockview はそこでパネルを移す)
let openPatched = false;
function patchWindowOpen() {
  if (openPatched) return;
  openPatched = true;
  const original = window.open.bind(window);
  window.open = (url?: string | URL, target?: string, features?: string) => {
    const w = original(url, target, features);
    if (w && String(url ?? '') === 'about:blank') {
      let loaded = false;
      try {
        w.addEventListener('load', () => (loaded = true));
      } catch {
        return w;
      }
      window.setTimeout(() => {
        try {
          if (!loaded && !w.closed && w.document.readyState === 'complete') w.dispatchEvent(new Event('load'));
        } catch {
          /* 触れない窓は dockview が失敗として扱う */
        }
      }, 50);
    }
    return w;
  };
}

function defaultsForPreview(format: string) {
  const depth = format.startsWith('D');
  return { channels: depth ? 0x1 : 0x7, depthLinear: depth, rangeMin: 0, rangeMax: depth ? 50 : 1, exposure: 0, gamma: !depth, mip: 0, pin: null };
}

function logError(text: string) {
  studioStore.addLog({ timeMs: performance.now(), level: 'error', source: 'ui', id: 0, text });
}

export const PREVIEW_PREFIX = 'preview:';
export const ASSET_PREFIX = 'asset:';
export const PIPELINE_PREFIX = 'pipeline:';

// 編集のタブの置き場所: ほかの編集のタブがあればその隣、なければビューポートの右
function openEditorTab(id: string, component: string, title: string, params: Record<string, unknown>) {
  if (!dockApi) return;
  const existing = dockApi.getPanel(id);
  if (existing) {
    existing.api.setActive();
    return;
  }
  const other = dockApi.panels.find((p) => p.id.startsWith(ASSET_PREFIX) || p.id.startsWith(PIPELINE_PREFIX));
  const viewport = dockApi.getPanel('viewport');
  dockApi.addPanel({
    id,
    component,
    title,
    params,
    position: other
      ? { referencePanel: other.id, direction: 'within' }
      : viewport
        ? { referencePanel: viewport.id, direction: 'right' }
        : undefined,
    initialWidth: 560,
  });
}

export function openAssetTab(path: string) {
  openEditorTab(ASSET_PREFIX + path, 'asset', path.slice(path.lastIndexOf('/') + 1), { path });
}

export function openPipelineTab(path: string) {
  openEditorTab(PIPELINE_PREFIX + path, 'pipeline', `パイプライン: ${path.slice(path.lastIndexOf('/') + 1)}`, { id: path });
}

// リソースのプレビューをタブで開く。同じリソースのタブがあれば前に出す。
// 置き場所: ほかのプレビューのタブがあればその隣 (同じグループ)、なければビューポートの右
export function openPreviewTab(resource: string, format: string) {
  if (!dockApi) return;
  const id = PREVIEW_PREFIX + resource;
  const existing = dockApi.getPanel(id);
  if (existing) {
    existing.api.setActive();
    return;
  }
  const other = dockApi.panels.find((p) => p.id.startsWith(PREVIEW_PREFIX));
  const viewport = dockApi.getPanel('viewport');
  dockApi.addPanel({
    id,
    component: 'preview',
    title: `プレビュー: ${resource}`,
    params: { resource, format },
    position: other
      ? { referencePanel: other.id, direction: 'within' }
      : viewport
        ? { referencePanel: viewport.id, direction: 'right' }
        : undefined,
    initialWidth: 420,
  });
}

// at はスクリーン座標 (CSS px)。窓のタイトルバーのあたりがそこに来るように置く
export async function popoutPanel(id: string, at?: { x: number; y: number }): Promise<void> {
  if (id.startsWith(PREVIEW_PREFIX)) {
    // プレビューのタブはネイティブの子ウィンドウなので、dockview の popout ではなく、ネイティブの別ウィンドウで開き直す
    const panel = dockApi?.getPanel(id);
    const params = (panel?.params ?? {}) as { resource?: string; format?: string };
    if (!panel || !params.resource || !isConnected()) return;
    try {
      await call('preview.open', {
        resource: params.resource,
        ...defaultsForPreview(params.format ?? ''),
        ...(at ? { x: at.x, y: at.y, dpr: window.devicePixelRatio || 1 } : {}),
      });
      panel.api.close();
    } catch (e) {
      logError(`プレビューを別ウィンドウにできません: ${errorText(e)}`);
    }
    return;
  }
  if (id === 'viewport') {
    if (!isConnected()) return;
    try {
      const r = await call('viewport.float', { floating: true, ...(at ? { x: at.x, y: at.y, dpr: window.devicePixelRatio || 1 } : {}) });
      studioStore.setViewportFloating(r.floating);
    } catch (e) {
      logError(`ビューポートを別ウィンドウにできません: ${errorText(e)}`);
    }
    return;
  }
  const panel = dockApi?.getPanel(id);
  if (!dockApi || !panel) return;
  const width = Math.max(360, Math.round(panel.api.width || 480));
  const height = Math.max(240, Math.round(panel.api.height || 360));
  patchWindowOpen();
  const left = at ? Math.round(at.x - width / 2) : window.screenX + 80;
  const top = at ? Math.round(at.y - 12) : window.screenY + 80;
  const ok = await dockApi.addPopoutGroup(panel, { popoutUrl: popoutUrl(), position: { left, top, width, height } });
  if (!ok) logError(`${PANEL_TITLES[id] ?? id} を別ウィンドウにできませんでした`);
}

// タブ (またはグループ) をドラッグして、ウィンドウの外で離したら別ウィンドウにする
export function watchDragOut(api: DockviewApi): () => void {
  let dragged: string | null = null;
  const d1 = api.onWillDragPanel((e) => (dragged = e.panel.id));
  const d2 = api.onWillDragGroup((e) => (dragged = e.group.activePanel?.id ?? null));
  const onDragEnd = (e: DragEvent) => {
    const id = dragged;
    dragged = null;
    if (!id) return;
    // ウィンドウの外 (クライアント領域の外) で離した。中で離したときは dockview が配置を変える
    const inside = e.clientX >= 0 && e.clientY >= 0 && e.clientX <= window.innerWidth && e.clientY <= window.innerHeight;
    const nowhere = e.screenX === 0 && e.screenY === 0; // 取り消し (Esc) では座標が 0 になる
    if (inside || nowhere || e.dataTransfer?.dropEffect !== 'none') return;
    void popoutPanel(id, { x: e.screenX, y: e.screenY });
  };
  document.addEventListener('dragend', onDragEnd, true);
  return () => {
    d1.dispose();
    d2.dispose();
    document.removeEventListener('dragend', onDragEnd, true);
  };
}
