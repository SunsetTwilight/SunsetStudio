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

// JS → C++ (studio.call)
export interface Methods {
  'ui.ready': { params: Record<string, never>; result: { protocol: number } };
  'app.info': { params: Record<string, never>; result: AppInfo };
  'app.ping': { params: unknown; result: { echo: unknown; uptimeMs: number } };
}

// C++ → JS (studio.on)
export interface Events {
  log: LogEntry;
  'engine.status': EngineInfo;
}
