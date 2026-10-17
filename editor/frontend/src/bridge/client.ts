// ============================================================
//  C++ との橋渡し (docs/design/editor-shell.md の「C++ と JS の橋渡し」)
//
//    call(method, params) → window.__studio_call (webview_bind)。Promise で結果を受け取る
//    on(name, handler)    ← window.__studio_event (C++ が webview_eval で呼ぶ)
//
//  ブラウザで直接開いた場合 (npm run dev をブラウザで見る) は接続されていないので、call は失敗する。
// ============================================================
import type { Events, Methods } from './protocol';

declare global {
  interface Window {
    __studio_call?: (method: string, params?: unknown) => Promise<unknown>;
    __studio_event?: (name: string, payload: unknown) => void;
    __studio_take_pending?: () => [string, unknown][];
  }
}

export class StudioError extends Error {
  readonly code: number;
  constructor(code: number, message: string) {
    super(message);
    this.code = code;
    this.name = 'StudioError';
  }
}

export function isConnected(): boolean {
  return typeof window.__studio_call === 'function';
}

export async function call<M extends keyof Methods>(
  method: M,
  params?: Methods[M]['params'],
): Promise<Methods[M]['result']> {
  const fn = window.__studio_call;
  if (!fn) {
    throw new StudioError(-32000, 'エディターに接続されていません (ブラウザで直接開いています)');
  }
  try {
    return (await fn(method, params ?? {})) as Methods[M]['result'];
  } catch (e) {
    const err = e as { code?: number; message?: string } | undefined;
    throw new StudioError(err?.code ?? -32603, err?.message ?? String(e));
  }
}

type Listener = (payload: unknown) => void;
const listeners = new Map<string, Set<Listener>>();

function dispatch(name: string, payload: unknown): void {
  const set = listeners.get(name);
  if (!set) return;
  for (const fn of [...set]) {
    try {
      fn(payload);
    } catch (e) {
      console.error(`[studio] イベント '${name}' の処理で例外`, e);
    }
  }
}

// イベントを受け取る。戻り値の関数で解除する
export function on<E extends keyof Events>(name: E, handler: (payload: Events[E]) => void): () => void {
  let set = listeners.get(name);
  if (!set) {
    set = new Set();
    listeners.set(name, set);
  }
  const fn = handler as Listener;
  set.add(fn);
  return () => set.delete(fn);
}

// C++ からのイベントの受け取り口を差し替え、それまでにたまったイベントを配る。起動時に 1 回呼ぶ
export function installEventSink(): void {
  window.__studio_event = dispatch;
  const pending = window.__studio_take_pending?.() ?? [];
  for (const [name, payload] of pending) dispatch(name, payload);
}
