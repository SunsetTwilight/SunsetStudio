// ============================================================
//  プレビューのパネル (M4): グラフのリソース 1 つを、タブのパネルに表示する
//
//  ノードエディターの右クリックの「タブ」で開く (state/dock.ts の openPreviewTab)。
//  表示はビューポートと同じ方式: C++ がメインのウィンドウの子ウィンドウ (preview.open の mode = "panel") を作り、
//  この要素の矩形に重ねる (preview.setRect)。パネルを閉じると preview.close。
//  タブをウィンドウの外へドラッグすると、ネイティブの別ウィンドウにする (state/dock.ts の popoutPanel)。
// ============================================================
import { useEffect, useRef, useState } from 'react';
import type { IDockviewPanelProps } from 'dockview-react';
import { call, isConnected } from '../bridge/client';
import type { ViewportRect } from '../bridge/protocol';
import { popoutPanel } from '../state/dock';
import { asObject, errorText } from '../state/graph';
import { useStudio } from '../state/store';
import { defaultsFor } from './ViewerPanel';

export interface PreviewParams {
  resource: string;
  format: string;
}

export function PreviewPanel(props: IDockviewPanelProps<PreviewParams>) {
  const { resource, format } = props.params;
  const ref = useRef<HTMLDivElement>(null);
  const [id, setId] = useState<number | null>(null);
  const [error, setError] = useState('');
  const graph = useStudio((s) => s.graph);
  const status = useStudio((s) => s.previews.find((p) => p.id === id)?.status ?? '');

  // 子ウィンドウを作り、パネルを閉じたら消す
  useEffect(() => {
    if (!isConnected()) return;
    let closed = false;
    let myId: number | null = null;
    call('preview.open', { ...defaultsFor(resource, format), pin: null, resource, mode: 'panel' })
      .then((r) => {
        if (closed) return void call('preview.close', { id: r.id }).catch(() => {});
        myId = r.id;
        setId(r.id);
      })
      .catch((e) => setError(errorText(e)));
    return () => {
      closed = true;
      if (myId !== null) void call('preview.close', { id: myId }).catch(() => {});
    };
  }, [resource, format]);

  // この要素の矩形に子ウィンドウを合わせる (ViewportPanel と同じ。ドラッグ中は隠す)
  useEffect(() => {
    if (id === null) return;
    let last = '';
    let dragging = false;
    let raf = 0;
    const tick = () => {
      raf = requestAnimationFrame(tick);
      const el = ref.current;
      if (!el) return;
      const r = el.getBoundingClientRect();
      const rect: ViewportRect = {
        x: r.left,
        y: r.top,
        width: r.width,
        height: r.height,
        dpr: window.devicePixelRatio || 1,
        // dockview の popout の窓に移されたときは、メインのウィンドウの座標ではないので隠す
        visible: !dragging && r.width > 0 && r.height > 0 && el.isConnected && el.ownerDocument === document && document.visibilityState === 'visible',
      };
      const key = JSON.stringify(rect);
      if (key === last) return;
      last = key;
      call('preview.setRect', { id, ...rect }).catch(() => {});
    };
    raf = requestAnimationFrame(tick);
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
    };
  }, [id]);

  const info = graph ? asObject(asObject(graph.graph.resources)[resource]) : null;
  return (
    <div className="panel preview-panel">
      <header className="panel-header">
        <span className="panel-title">{resource}</span>
        <span className="muted">{info ? String(info.format ?? format) : format}</span>
        <span className="spacer" />
        <button onClick={() => void popoutPanel(props.api.id)} title="このプレビューを別ウィンドウにする (タブをウィンドウの外へドラッグしても同じ)">
          別ウィンドウ
        </button>
      </header>
      {(error || status) && <div className={`inspector-status ${error ? 'error' : 'busy'}`}>{error || status}</div>}
      <div className="preview-area" ref={ref}>
        <div className="placeholder">{isConnected() ? 'プレビューを準備しています…' : 'エディターの中で表示されます'}</div>
      </div>
    </div>
  );
}
