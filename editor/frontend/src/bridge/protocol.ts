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

// ---- スキーマ (gfxDescribeStruct / gfxDescribeEnum の JSON) ----
export type FieldType =
  | 'bool'
  | 'uint8'
  | 'int32'
  | 'uint32'
  | 'float'
  | 'enum'
  | 'flags'
  | 'struct'
  | 'string'
  | 'handle'
  | 'cstring'
  | 'pointer';

export interface FieldDesc {
  name: string;
  type: FieldType;
  enum?: string;
  struct?: string;
  count: number; // 1 = スカラー。string のときは最大のバイト数 (終端を含む)
  min?: number;
  max?: number;
  serialized: boolean; // false = ハンドルなど (JSON に出ない。表示だけ)
  default?: unknown;
  tooltip?: string; // 対応する D3D12 のメンバー
}

export interface StructDesc {
  name: string;
  size: number;
  fields: FieldDesc[];
}

export interface EnumDesc {
  name: string;
  flags: boolean;
  values: { name: string; value: number }[];
}

export interface TypeDescription {
  type: string;
  structs: Record<string, StructDesc>;
  enums: Record<string, EnumDesc>;
}

export type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };
export type JsonObject = { [key: string]: JsonValue };

export interface PipelineInfo {
  id: string;
  label: string;
  type: string;
}

// JS → C++ (studio.call)
export interface Methods {
  'schema.types': { params: Record<string, never>; result: { structs: string[]; enums: string[] } };
  'schema.describe': { params: { type: string }; result: TypeDescription };
  'pipeline.list': { params: Record<string, never>; result: PipelineInfo[] };
  'pipeline.get': { params: { id: string }; result: { id: string; type: string; desc: JsonObject } };
  'pipeline.update': {
    params: { id: string; desc: JsonObject };
    result: { id: string; desc: JsonObject; warnings: string[] };
  };
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
