// ============================================================
//  リソースビューアーのパネル (M4)
//
//  レンダーグラフの途中のテクスチャ (G-Buffer、深度など) を、ビューポートに最終画像の代わりに表示する。
//  チャンネル、mip、深度の線形化、露出、表示する範囲、ガンマを選び、カーソルの下のピクセルの値を見る。
//  設定は viewer.set でエディターに送り、ピクセルの値は viewer.pixel のイベントで届く。
// ============================================================
import { useEffect, useState } from 'react';
import { call, isConnected } from '../bridge/client';
import type { ViewerSettings } from '../bridge/protocol';
import { asObject, errorText } from '../state/graph';
import { studioStore, useStudio } from '../state/store';

const CHANNELS = ['R', 'G', 'B', 'A'];

export function ViewerPanel() {
  const graph = useStudio((s) => s.graph);
  const viewer = useStudio((s) => s.viewer);
  const pixel = useStudio((s) => s.viewerPixel);
  const [error, setError] = useState('');

  useEffect(() => {
    if (!isConnected() || !graph) return;
    call('viewer.get')
      .then(studioStore.setViewer)
      .catch((e) => setError(errorText(e)));
  }, [graph === null]);

  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;
  if (!graph || !viewer) return <div className="panel"><div className="placeholder">プロジェクトのグラフを待っています…</div></div>;

  const resources = asObject(graph.graph.resources);
  const textures = Object.entries(resources)
    .filter(([, v]) => (asObject(v).kind ?? 'texture') === 'texture')
    .map(([name, v]) => ({ name, format: String(asObject(v).format ?? ''), mips: Number(asObject(v).mips ?? 1) }));
  const current = textures.find((t) => t.name === viewer.resource);
  const isDepth = !!current && current.format.startsWith('D');

  return (
    <div className="panel viewer-panel">
      <header className="panel-header">
        <label className="viewer-toggle">
          <input type="checkbox" checked={viewer.enabled} onChange={(e) => void setViewer({ enabled: e.target.checked }, setError)} />
          ビューポートに表示
        </label>
        <select
          value={viewer.resource}
          onChange={(e) => {
            const t = textures.find((x) => x.name === e.target.value);
            void setViewer(defaultsFor(e.target.value, t?.format ?? ''), setError);
          }}
        >
          <option value="">(リソースを選ぶ)</option>
          {textures.map((t) => (
            <option key={t.name} value={t.name}>
              {t.name} ({t.format})
            </option>
          ))}
        </select>
        <button
          disabled={!viewer.resource}
          title="このリソースを別ウィンドウに表示する (今の設定で)"
          onClick={() => void openPreview(viewer.resource, current?.format ?? '', undefined, viewer).then(setError)}
        >
          別ウィンドウ
        </button>
      </header>
      {error && <div className="inspector-status error">{error}</div>}
      {viewer.enabled && pixel?.status && <div className="inspector-status busy">{pixel.status}</div>}

      <div className="viewer-body">
        <div className="viewer-controls">
          <span className="viewer-group">
            {CHANNELS.map((c, i) => (
              <label key={c} className={`viewer-channel ch-${c}`}>
                <input
                  type="checkbox"
                  checked={(viewer.channels & (1 << i)) !== 0}
                  onChange={(e) => void setViewer({ channels: e.target.checked ? viewer.channels | (1 << i) : viewer.channels & ~(1 << i) }, setError)}
                />
                {c}
              </label>
            ))}
          </span>
          {current && current.mips > 1 && (
            <span className="viewer-group">
              mip
              <select value={viewer.mip} onChange={(e) => void setViewer({ mip: Number(e.target.value) }, setError)}>
                {Array.from({ length: current.mips }, (_, i) => (
                  <option key={i} value={i}>
                    {i}
                  </option>
                ))}
              </select>
            </span>
          )}
          {isDepth && (
            <label className="viewer-group">
              <input type="checkbox" checked={viewer.depthLinear} onChange={(e) => void setViewer({ depthLinear: e.target.checked }, setError)} />
              距離にする
            </label>
          )}
          <label className="viewer-group">
            <input type="checkbox" checked={viewer.gamma} onChange={(e) => void setViewer({ gamma: e.target.checked }, setError)} />
            ガンマ
          </label>
        </div>
        <div className="viewer-controls">
          <span className="viewer-group" title="露出 (EV)。表示する前に 2^EV 倍する (深度を距離にするときは使わない)">
            露出
            <input
              type="range"
              min={-8}
              max={8}
              step={0.25}
              value={viewer.exposure}
              disabled={isDepth && viewer.depthLinear}
              onChange={(e) => void setViewer({ exposure: Number(e.target.value) }, setError)}
            />
            <span className="viewer-num">{viewer.exposure.toFixed(2)}</span>
          </span>
          <span className="viewer-group" title={isDepth && viewer.depthLinear ? '表示する距離の範囲' : '表示する値の範囲 (この範囲を黒〜白にする)'}>
            範囲
            <RangeBox value={viewer.rangeMin} onCommit={(v) => void setViewer({ rangeMin: v }, setError)} />〜
            <RangeBox value={viewer.rangeMax} onCommit={(v) => void setViewer({ rangeMax: v }, setError)} />
          </span>
        </div>

        <div className="viewer-pixel">
          {pixel?.pixel && viewer.enabled ? (
            <>
              <span className="muted">
                {pixel.pixel.resource} ({pixel.pixel.x}, {pixel.pixel.y}) mip {pixel.pixel.mip}
              </span>
              <span className="viewer-swatch" style={{ background: swatch(pixel.pixel.value) }} />
              {pixel.pixel.distance !== undefined ? (
                <>
                  <span className="viewer-value">深度 {formatValue(pixel.pixel.value[0])}</span>
                  <span className="viewer-value">距離 {formatValue(pixel.pixel.distance)}</span>
                </>
              ) : (
                pixel.pixel.value.map((v, i) => (
                  <span key={i} className={`viewer-value ch-${CHANNELS[i]}`}>
                    {CHANNELS[i]} {formatValue(v)}
                  </span>
                ))
              )}
            </>
          ) : (
            <span className="muted">{viewer.enabled ? 'ビューポートにカーソルを置くと、そのピクセルの値を表示します' : '「ビューポートに表示」をオンにすると、選んだリソースを表示します'}</span>
          )}
        </div>
      </div>
    </div>
  );
}

// リソースの種類に合わせた既定の表示
function defaultsFor(resource: string, format: string): Partial<ViewerSettings> {
  const depth = format.startsWith('D');
  return {
    resource,
    enabled: resource !== '',
    mip: 0,
    channels: depth ? 0x1 : 0x7,
    depthLinear: depth,
    rangeMin: 0,
    rangeMax: depth ? 50 : 1,
    exposure: 0,
    gamma: !depth,
  };
}

export async function setViewer(patch: Partial<ViewerSettings>, onError?: (e: string) => void): Promise<void> {
  try {
    onError?.('');
    studioStore.setViewer(await call('viewer.set', patch));
  } catch (e) {
    onError?.(errorText(e));
  }
}

// リソースを選んでビューアーに表示する (インスペクターなどから)
export function showInViewer(resource: string, format: string) {
  void setViewer(defaultsFor(resource, format));
}

// リソースを別ウィンドウに表示する。settings を省くとフォーマットに合う既定値。at はスクリーン座標 (CSS px)。
// 失敗したら理由を返す
export async function openPreview(
  resource: string,
  format: string,
  at?: { x: number; y: number },
  settings?: Partial<ViewerSettings>,
): Promise<string> {
  try {
    await call('preview.open', {
      ...(settings ?? defaultsFor(resource, format)),
      pin: null,
      resource,
      ...(at ? { x: at.x, y: at.y, dpr: window.devicePixelRatio || 1 } : {}),
    });
    return '';
  } catch (e) {
    return errorText(e);
  }
}

function formatValue(v: number): string {
  if (!Number.isFinite(v)) return String(v);
  const a = Math.abs(v);
  return a !== 0 && (a < 0.001 || a >= 10000) ? v.toExponential(3) : v.toFixed(4);
}

function swatch(v: number[]): string {
  const c = (x: number) => Math.round(Math.pow(Math.min(Math.max(x, 0), 1), 1 / 2.2) * 255);
  return `rgb(${c(v[0])}, ${c(v[1])}, ${c(v[2])})`;
}

function RangeBox({ value, onCommit }: { value: number; onCommit: (v: number) => void }) {
  const [text, setText] = useState(String(value));
  useEffect(() => setText(String(value)), [value]);
  const commit = () => {
    const v = Number(text);
    if (Number.isFinite(v) && v !== value) onCommit(v);
    else setText(String(value));
  };
  return (
    <input
      className="number viewer-range"
      value={text}
      onChange={(e) => setText(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => e.key === 'Enter' && commit()}
    />
  );
}
