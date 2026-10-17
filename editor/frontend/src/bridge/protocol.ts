// ============================================================
//  C++ (SunsetEditor) とやり取りするメソッドとイベントの型。
//  C++ 側は editor/src/app/EditorApp.cpp。形を変えたら両方を直し、互換性がなければ PROTOCOL_VERSION を上げる。
// ============================================================

export const PROTOCOL_VERSION = 1;

export interface EngineInfo {
  status: 'starting' | 'ready' | 'failed';
  abi: number;
  adapter?: string;
  api?: string;
  error?: string;
}

export interface AppInfo {
  app: string;
  version: string;
  protocol: number;
  engine: EngineInfo;
  methods: string[];
}

export type LogLevel = 'error' | 'warning' | 'info';

export interface LogEntry {
  timeMs: number;
  level: LogLevel;
  source: string; // app / engine / shader / pipeline / d3d12
  id: number;
  text: string;
}

// ビューポートのフレームの統計 (約 0.5 秒ごと)
export interface FrameStats {
  fps: number;
  frameMs: number;
  cpuMs: number; // 記録と submit (Present の待ちを除く)
  width: number;
  height: number;
  frames: number;
}

// ビューポートの矩形。WebView のクライアント座標の CSS px と devicePixelRatio
export interface ViewportRect {
  x: number;
  y: number;
  width: number;
  height: number;
  dpr: number;
  visible: boolean;
}

// JS → C++ (studio.call)
export interface Methods {
  'ui.ready': { params: Record<string, never>; result: { protocol: number } };
  'app.info': { params: Record<string, never>; result: AppInfo };
  'app.ping': { params: unknown; result: { echo: unknown; uptimeMs: number } };
  'viewport.setRect': { params: ViewportRect; result: { width: number; height: number; visible: boolean } };
}

// C++ → JS (studio.on)
export interface Events {
  log: LogEntry;
  'engine.status': EngineInfo;
  stats: FrameStats;
}
