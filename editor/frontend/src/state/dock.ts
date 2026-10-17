// ============================================================
//  パネルの配置 (dockview) の操作。ツールバーやノードエディターから使う
//
//    showPanel      パネルを前に出す (閉じていたら開く)
//    popoutPanel    パネルを別ウィンドウにする。Web のパネルは dockview の popout (WebView2 の window.open)、
//                   ビューポートはネイティブの子ウィンドウなので、C++ が別ウィンドウに移す (viewport.float)
//    watchDragOut   タブをウィンドウの外へドラッグして離したら、そのパネルを別ウィンドウにする
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
};

// パネルがなければ開く (保存した配置から閉じたパネルを戻す)
export function showPanel(id: string) {
  if (!dockApi) return;
  const existing = dockApi.getPanel(id);
  if (existing) {
    existing.api.setActive();
    return;
  }
  const ref = dockApi.getPanel('nodes') ?? dockApi.getPanel('log') ?? dockApi.getPanel('viewport');
  dockApi.addPanel({
    id,
    component: id,
    title: PANEL_TITLES[id] ?? id,
    position: ref ? { referencePanel: ref.id, direction: 'within' } : undefined,
  });
}

// popout の窓に読み込むページ。配布時の UI は about:blank (webview_set_html) なので、同じ about:blank を開く。
// 開発サーバーでは public/popout.html
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

function logError(text: string) {
  studioStore.addLog({ timeMs: performance.now(), level: 'error', source: 'ui', id: 0, text });
}

// at はスクリーン座標 (CSS px)。窓のタイトルバーのあたりがそこに来るように置く
export async function popoutPanel(id: string, at?: { x: number; y: number }): Promise<void> {
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
