// ============================================================
//  ビューポートのパネル
//
//  この要素の矩形に、C++ がエンジンの子ウィンドウ (ネイティブ) を重ねる。
//  矩形はパネルの移動 / 分割 / ウィンドウのリサイズで変わるので、requestAnimationFrame ごとに
//  getBoundingClientRect を調べ、変わったときだけ viewport.setRect を送る。
//
//  子ウィンドウは Web の UI より手前に出るので、ビューポートの上に UI を重ねられない。
//  パネルをドラッグしているあいだ (ドロップ先の表示がビューポートに重なる) は隠す。
//  ビューポートを別ウィンドウにしているあいだ (viewport.float) は、C++ は矩形を覚えるだけで、ここには案内を出す。
// ============================================================
import { useEffect, useRef } from 'react';
import { call, isConnected } from '../bridge/client';
import type { ViewportRect } from '../bridge/protocol';
import { studioStore, useStudio } from '../state/store';

function sameRect(a: ViewportRect | null, b: ViewportRect): boolean {
  return (
    !!a &&
    a.visible === b.visible &&
    a.dpr === b.dpr &&
    Math.abs(a.x - b.x) < 0.25 &&
    Math.abs(a.y - b.y) < 0.25 &&
    Math.abs(a.width - b.width) < 0.25 &&
    Math.abs(a.height - b.height) < 0.25
  );
}

export function ViewportPanel() {
  const ref = useRef<HTMLDivElement>(null);
  const engine = useStudio((s) => s.engine);
  const floating = useStudio((s) => s.viewportFloating);

  useEffect(() => {
    if (!isConnected()) return;
    let last: ViewportRect | null = null;
    let dragging = false;
    let raf = 0;

    const send = (rect: ViewportRect) => {
      if (sameRect(last, rect)) return;
      last = rect;
      call('viewport.setRect', rect).catch((e) => console.error('[viewport] setRect', e));
    };

    const tick = () => {
      raf = requestAnimationFrame(tick);
      const el = ref.current;
      if (!el) return;
      const r = el.getBoundingClientRect();
      send({
        x: r.left,
        y: r.top,
        width: r.width,
        height: r.height,
        dpr: window.devicePixelRatio || 1,
        visible: !dragging && r.width > 0 && r.height > 0 && el.isConnected && document.visibilityState === 'visible',
      });
    };
    raf = requestAnimationFrame(tick);

    // dockview のパネルのドラッグ (HTML のドラッグ & ドロップ) のあいだは隠す
    const onDragStart = () => (dragging = true);
    const onDragEnd = () => (dragging = false);
    document.addEventListener('dragstart', onDragStart, true);
    document.addEventListener('dragend', onDragEnd, true);
    document.addEventListener('drop', onDragEnd, true);

    return () => {
      cancelAnimationFrame(raf);
      document.removeEventListener('dragstart', onDragStart, true);
      document.removeEventListener('dragend', onDragEnd, true);
      document.removeEventListener('drop', onDragEnd, true);
      call('viewport.setRect', { x: 0, y: 0, width: 0, height: 0, dpr: 1, visible: false }).catch(() => {});
    };
  }, [floating]); // 別ウィンドウから戻ったら、矩形を送り直す

  return (
    <div className="viewport" data-viewport ref={ref}>
      <div className="placeholder">
        {floating ? (
          <div className="viewport-floating">
            ビューポートは別ウィンドウに表示しています
            <button
              onClick={() =>
                void call('viewport.float', { floating: false })
                  .then((r) => studioStore.setViewportFloating(r.floating))
                  .catch(() => {})
              }
            >
              ここに戻す
            </button>
          </div>
        ) : !isConnected()
          ? '3D ビューポート (エディターの中で表示されます)'
          : engine?.status === 'failed'
            ? `エンジンを初期化できませんでした: ${engine.error ?? ''}`
            : 'ビューポートを準備しています…'}
      </div>
    </div>
  );
}
