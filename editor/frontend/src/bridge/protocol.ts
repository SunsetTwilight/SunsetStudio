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
  window?: { customFrame: boolean; maximized: boolean; devTools: boolean }; // UI が描くタイトルバー
}

export type LogLevel = 'error' | 'warning' | 'info';

export interface LogEntry {
  timeMs: number;
  level: LogLevel;
  source: string; // app / engine / shader / pipeline / d3d12
  id: number;
  text: string;
}

// レンダーグラフのパスごとの GPU 時間
export interface PassTiming {
  name: string;
  gpuMs: number;
}

// ビューポートのフレームの統計 (約 0.5 秒ごと)
export interface FrameStats {
  fps: number;
  frameMs: number;
  cpuMs: number; // 記録と submit (Present の待ちを除く)
  width: number;
  height: number;
  frames: number;
  gpuMs: number; // グラフ全体の GPU 時間 (数フレーム前のもの)
  passes: PassTiming[]; // パスごとの GPU 時間 (実行順)
}

// ビューポートの矩形。WebView のクライアント座標の CSS px と devicePixelRatio
export interface ViewportRect {
  x: number;
  y: number;
  width: number;
  height: number;
  dpr: number;
  visible: boolean;
  // ビューポートに重なる Web の要素 (メニューなど) の矩形 (CSS px)。その部分を切り抜いて Web を見せる
  holes?: { x: number; y: number; width: number; height: number }[];
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
  id: string; // パイプラインのアセットのパス ("pipelines/gbuffer.json")
  label: string;
  type: string;
}

// ---- レンダーグラフ (ビューポート) ----
export type SkipReason = 'disabled' | 'condition' | 'culled';

export interface GraphDiagnostic {
  level: 'error' | 'warning';
  path: string; // "passes[Lighting].inputs[0].bind"
  text: string;
}

export interface GraphResourceInfo {
  name: string;
  kind: string; // texture / buffer / imported
  used: boolean;
  firstPass: number;
  lastPass: number;
}

export interface GraphInfo {
  file: string; // "graphs/viewport.json"
  revision: number; // 切り替わるたびに増える
  graph: JsonObject; // グラフのアセットの JSON (エンジンの graphToJson と同じ形)
  order: string[]; // 実行するパス (実行順)
  skipped: { name: string; reason: SkipReason }[];
  diagnostics: GraphDiagnostic[];
  resources: GraphResourceInfo[];
  pipelines: string[]; // プロジェクトのパイプラインのアセット
}

// ---- プロジェクト (M4a) ----
export interface ProjectInfo {
  open: boolean;
  name?: string;
  file?: string; // .sunsetproj の絶対パス
  dir?: string;
  assetRoot?: string;
  engine?: string;
  graph?: string;
  legacyLayout?: boolean; // 古い構成 (Assets/ の下)。project.migrate で直下に並べる構成に移せる
  scene?: string; // ビューポートのシーン (アセットのパス)
}

// シーン (scene.*、M5-2)
export interface SceneObjectJson {
  id: string;
  name: string;
  model: string; // "builtin:cube"
  parent: string | null;
  position: [number, number, number];
  rotation: [number, number, number]; // 度 (X → Y → Z の順に回す)
  scale: [number, number, number];
  visible: boolean;
  color: [number, number, number, number]; // 仮 (M5-4 でマテリアルに)
  layer: string; // 仮 (レンダーグラフの drawList の layer)
}
export interface SceneJson {
  version: number;
  objects: SceneObjectJson[];
  editor?: JsonValue;
}
export interface SceneInfo {
  file: string;
  revision: number;
  dirty: boolean;
  scene: SceneJson;
  error?: string; // 読めなかった理由 (編集 / 保存はできない)
}

// アセットのライブラリ (M5-3): プロジェクトの assets/ (SunsetAsset)
export interface LibraryModelInfo {
  nodes: number;
  meshes: number;
  vertices: number;
  triangles: number;
  slots: number;
  skins: number;
  morphs: number;
  boundsMin: [number, number, number];
  boundsMax: [number, number, number];
  loadMs: number;
}
export interface LibraryModelState {
  status: 'loading' | 'ready' | 'error';
  error?: string;
  info?: LibraryModelInfo;
}
export interface LibraryAsset {
  id: string; // 小文字の 16 進 32 文字 (シーンは "asset:<id>" で参照する)
  path: string; // assets/ からの相対パス
  importer: string; // "model" / "texture" …
  external: boolean; // 外部のファイルを参照している (コピーしていない)
  source?: string; // 外部のファイルのパス
  missing: boolean; // 元のファイルがない
  cached: boolean; // キャッシュ (Cache/assets) が新しい
  model?: LibraryModelState; // 読んだ (読んでいる) モデル
}
export interface LibraryInfo {
  assets: LibraryAsset[];
  extensions: string[];
}
export interface LibraryImportResult {
  canceled?: boolean;
  id?: string;
  path?: string;
  copied?: string[];
  warnings?: string[];
}

export interface RecentProject {
  file: string;
  name: string;
  exists: boolean;
}

export interface ProjectTemplate {
  id: string;
  label: string;
}

// ---- 編集の履歴 (Undo / Redo) ----
export interface EditState {
  canUndo: boolean;
  canRedo: boolean;
  undoLabel: string;
  redoLabel: string;
  undoCount: number;
  redoCount: number;
}

// ---- リソースビューアー ----
// シェーダー / パイプラインの編集 (asset.* / shader.*)
export interface AssetTemplate {
  id: string;
  kind: 'shader' | 'pipeline';
  label: string;
  folder: string;
  suffix: string;
  stage: '' | 'vs' | 'ps' | 'cs';
  description: string;
}
export interface ShaderReflection {
  shaderModel: number;
  threadGroupSize: [number, number, number];
  instructionCount: number;
  truncated: boolean;
  bindings: JsonObject[]; // GfxShaderBinding (name, type, dimension, bindPoint, bindCount, space …)
  cbuffers: JsonObject[];
  variables: JsonObject[];
  inputs: JsonObject[]; // GfxShaderSignatureParam
  outputs: JsonObject[];
}
export interface ShaderCompileResult {
  path: string;
  stage: 'vs' | 'ps' | 'cs';
  ok: boolean;
  messages: string; // DXC のメッセージ (そのまま)
  reflection?: ShaderReflection;
}
export interface ShaderUsage {
  pipeline: string;
  stage: 'vs' | 'ps' | 'cs';
  entry: string;
  profile: string;
}
export interface ShaderStatus {
  path: string;
  ok: boolean;
  text: string;
}

// 別ウィンドウのプレビュー (preview.*)
export interface PreviewInfo {
  id: number;
  resource: string;
  status: string; // 表示できない理由 (空なら表示できている)
  mode: 'window' | 'panel'; // 別ウィンドウ / タブのパネル
}

export interface ViewerSettings {
  enabled: boolean;
  resource: string;
  mip: number;
  channels: number; // bit0 = R, bit1 = G, bit2 = B, bit3 = A
  depthLinear: boolean;
  exposure: number; // EV
  rangeMin: number;
  rangeMax: number;
  gamma: boolean;
  pin: [number, number] | null; // カーソルの代わりに読むテクセル
}

export interface ViewerPixel {
  status: string; // 表示できない理由 (空なら表示できている)
  pixel?: { resource: string; x: number; y: number; mip: number; value: number[]; distance?: number }; // distance: 深度のとき、視点からの距離
}

// JS → C++ (studio.call)
export interface Methods {
  'schema.types': { params: Record<string, never>; result: { structs: string[]; enums: string[] } };
  'schema.describe': { params: { type: string }; result: TypeDescription };
  'pipeline.list': { params: Record<string, never>; result: PipelineInfo[] };
  'pipeline.get': { params: { id: string }; result: { id: string; type: string; desc: JsonObject } };
  'pipeline.update': {
    params: { id: string; desc: JsonObject; label?: string };
    result: { id: string; desc: JsonObject; warnings: string[] };
  };
  'pipeline.save': { params: { id: string }; result: { id: string } };
  'graph.get': { params: Record<string, never>; result: GraphInfo };
  'graph.set': { params: { graph: JsonObject; label?: string }; result: GraphInfo };
  'edit.state': { params: Record<string, never>; result: EditState };
  'edit.undo': { params: Record<string, never>; result: { label: string; kind: string; target: string; state: EditState } };
  'edit.redo': { params: Record<string, never>; result: { label: string; kind: string; target: string; state: EditState } };
  'graph.save': { params: Record<string, never>; result: { revision: number } };
  'project.info': { params: Record<string, never>; result: ProjectInfo };
  'project.recent': { params: Record<string, never>; result: { projects: RecentProject[]; defaultLocation: string } };
  'project.templates': { params: Record<string, never>; result: ProjectTemplate[] };
  'project.open': { params: { file: string }; result: ProjectInfo };
  'project.create': { params: { location: string; name: string; template: string }; result: ProjectInfo };
  'project.close': { params: Record<string, never>; result: ProjectInfo };
  'project.migrate': { params: Record<string, never>; result: ProjectInfo };
  'scene.get': { params: Record<string, never>; result: SceneInfo };
  'scene.set': { params: { scene: SceneJson; label?: string }; result: { revision: number; dirty: boolean; warnings: string[] } };
  'scene.save': { params: Record<string, never>; result: { revision: number; dirty: boolean } };
  'scene.select': { params: { id: string }; result: { id: string } };
  'scene.pickAt': { params: { x: number; y: number }; result: { id: string } };
  'library.list': { params: Record<string, never>; result: LibraryInfo };
  'library.import': { params: { mode: 'copy' | 'link'; file?: string }; result: LibraryImportResult };
  'library.refresh': { params: Record<string, never>; result: { created: number; reIded: number; reloaded: string[] } };
  'library.reimport': { params: { id: string }; result: { id: string } };
  'library.load': { params: { id: string }; result: { id: string; status: string } };
  'window.minimize': { params: Record<string, never>; result: { maximized: boolean } };
  'window.toggleMaximize': { params: Record<string, never>; result: { maximized: boolean } };
  'window.close': { params: Record<string, never>; result: Record<string, never> };
  'window.startDrag': { params: Record<string, never>; result: Record<string, never> };
  'window.startResize': { params: { edge: 'top' | 'topleft' | 'topright' }; result: Record<string, never> };
  'window.devTools': { params: Record<string, never>; result: Record<string, never> };
  'project.layout.get': { params: Record<string, never>; result: { layout: JsonValue } };
  'project.layout.set': { params: { layout: JsonValue }; result: Record<string, never> };
  'dialog.openProject': { params: Record<string, never>; result: { file: string | null } };
  'dialog.pickFolder': { params: { initial?: string }; result: { folder: string | null } };
  'viewer.get': { params: Record<string, never>; result: ViewerSettings };
  'viewer.set': { params: Partial<ViewerSettings>; result: ViewerSettings };
  'viewer.pixel.get': { params: Record<string, never>; result: ViewerPixel };
  'ui.ready': { params: Record<string, never>; result: { protocol: number } };
  'app.info': { params: Record<string, never>; result: AppInfo };
  'app.ping': { params: unknown; result: { echo: unknown; uptimeMs: number } };
  'viewport.setRect': { params: ViewportRect; result: { width: number; height: number; visible: boolean; floating?: boolean } };
  'viewport.state': { params: Record<string, never>; result: { floating: boolean } };
  'viewport.float': { params: { floating: boolean; x?: number; y?: number; dpr?: number }; result: { floating: boolean } };
  'preview.open': {
    params: Partial<ViewerSettings> & { resource: string; mode?: 'window' | 'panel'; x?: number; y?: number; dpr?: number };
    result: { id: number };
  };
  'preview.setRect': { params: ViewportRect & { id: number }; result: Record<string, never> };
  'preview.set': { params: Partial<ViewerSettings> & { id: number }; result: ViewerSettings };
  'preview.close': { params: { id: number }; result: { previews: PreviewInfo[] } };
  'preview.list': { params: Record<string, never>; result: { previews: PreviewInfo[] } };
  'asset.list': { params: { dir: string; extensions?: string[] }; result: { files: string[] } };
  'asset.read': { params: { path: string }; result: { path: string; text: string } };
  'asset.write': { params: { path: string; text: string; create?: boolean }; result: { path: string } };
  'asset.templates': { params: Record<string, never>; result: { templates: AssetTemplate[] } };
  'asset.create': { params: { template: string; path: string; shader?: string }; result: { path: string; text: string } };
  'asset.openExternal': { params: { path: string; app: 'default' | 'vscode'; line?: number }; result: { opened: boolean } };
  'shader.compile': {
    params: { path: string; text?: string; stage?: 'vs' | 'ps' | 'cs'; entry?: string; profile?: string };
    result: ShaderCompileResult;
  };
  'shader.usages': { params: { path: string }; result: { usages: ShaderUsage[] } };
  'shader.status': { params: Record<string, never>; result: { errors: ShaderStatus[] } };
}

// C++ → JS (studio.on)
export interface Events {
  log: LogEntry;
  'engine.status': EngineInfo;
  stats: FrameStats;
  'graph.changed': { revision: number };
  'project.changed': ProjectInfo;
  'edit.changed': EditState;
  'pipeline.changed': { id: string };
  'viewer.pixel': ViewerPixel;
  'preview.changed': { previews: PreviewInfo[] };
  'viewport.changed': { floating: boolean };
  'shader.status': ShaderStatus;
  'scene.changed': { revision: number; dirty: boolean };
  'scene.picked': { id: string };
  'library.changed': Record<string, never>;
  'window.state': { maximized: boolean };
}
