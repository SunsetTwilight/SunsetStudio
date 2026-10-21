// ============================================================
//  ノードエディターのパネル (M4): ビューポートのレンダーグラフを、ノードと線で編集する
//
//    表示   パス (入力 / 出力、パイプライン、実行順、GPU 時間、実行しない理由、診断) とリソース (フォーマット、大きさ)。
//           線は 書く (パス → リソース、橙) と 読む (リソース → パス、灰)。ラベルは使い方 (RTV0 / SRV など)
//    編集   ドラッグで移動 (位置はグラフの editor に保存)、ハンドルどうしをつなぐ (入力 / 出力を加える・差し替える)、
//           Delete で線 / ノードを消す、右クリックでパス / リソースを加える、クリックでインスペクターに表示
//    結果   ノードを右クリック → そのノードの結果 (リソースならそれ自身、パスなら出力 / 入力のテクスチャ) だけを
//           ビューポートに表示する (リソースビューアー)、タブに表示する、または別ウィンドウに表示する (preview.open)
//    シェーダー パスを右クリック → パイプラインを開く、VS / PS / CS を開く (Monaco のタブ)、
//           新しいシェーダーを作る (パイプラインを複製し、新しいシェーダーに差し替えて、パスにつなぐ)
//    操作   自動配置 (dagre)、全体を表示
//  編集はすべてグラフ全体の JSON を graph.set で送る (コンパイルできなければエラーで前のまま、元に戻す / やり直すが効く)。
// ============================================================
import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import {
  Background,
  Controls,
  ReactFlow,
  ReactFlowProvider,
  applyNodeChanges,
  useReactFlow,
  type Connection,
  type FinalConnectionState,
  type Edge,
  type IsValidConnection,
  type Node,
  type NodeChange,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { isConnected } from '../bridge/client';
import type { GraphInfo, JsonObject } from '../bridge/protocol';
import { setViewer, showInViewer, openPreview } from '../panels/ViewerPanel';
import { saveAll } from '../state/commands';
import { openAssetTab, openPipelineTab, openPreviewTab, showPanel } from '../state/dock';
import { call } from '../bridge/client';
import { fileName, loadPipelineShaders, shaderErrorsOf, withShaderFile, type Stage } from '../state/assets';
import { NewShaderDialog } from '../panels/NewShaderDialog';
import { applyGraph, applyInputGraph } from '../state/graph';
import { studioStore, useStudio } from '../state/store';
import {
  addDrawPass,
  addOutput,
  addPass,
  addResource,
  autoLayout,
  buildModel,
  connect,
  drawNodeNames,
  drawNodeTarget,
  parseNodeId,
  previewTargets,
  removeEdges,
  removeNodes,
  savedPositions,
  withPositions,
  type PassKind,
  type PreviewTarget,
  type XY,
} from './model';
import { nodeTypes, type NodeRuntime } from './nodes';

const SKIP_LABEL: Record<string, string> = { disabled: '無効', condition: '条件が偽', culled: '結果に届かない' };

type Status = { kind: 'idle' | 'busy' | 'ok' | 'error'; text: string };

export function NodeEditorPanel() {
  if (!isConnected()) return <div className="panel"><div className="placeholder">エディターの中で表示されます</div></div>;
  return (
    <ReactFlowProvider>
      <NodeEditor />
    </ReactFlowProvider>
  );
}

// グラフの情報から、ノードごとの実行の情報を作る
function runtimeOf(graph: GraphInfo, gpu: Map<string, number>): Map<string, NodeRuntime> {
  const out = new Map<string, NodeRuntime>();
  const get = (id: string) => {
    let r = out.get(id);
    if (!r) out.set(id, (r = { errors: [], warnings: [] }));
    return r;
  };
  graph.order.forEach((name, i) => {
    const r = get(`pass:${name.replace(/\[\d+\]$/, '')}`);
    if (r.order === undefined) r.order = i + 1;
    r.gpuMs = gpu.get(name.replace(/\[\d+\]$/, ''));
  });
  for (const s of graph.skipped) get(`pass:${s.name}`).skipped = SKIP_LABEL[s.reason] ?? s.reason;
  for (const r of graph.resources) if (!r.used && !r.name.startsWith('$')) get(`res:${r.name}`).skipped = '使われていない';
  for (const d of graph.diagnostics) {
    const m = /^passes\[([^\]\[]+)/.exec(d.path) ?? null;
    const rm = /^resources\.([^.]+)/.exec(d.path);
    const id = m ? `pass:${m[1]}` : rm ? `res:${rm[1]}` : null;
    if (!id) continue;
    (d.level === 'error' ? get(id).errors : get(id).warnings).push(`${d.path}: ${d.text}`);
  }
  return out;
}

function NodeEditor() {
  const graph = useStudio((s) => s.graph);
  const stats = useStudio((s) => s.stats);
  const selection = useStudio((s) => s.selection);
  const flow = useReactFlow();
  const [nodes, setNodes] = useState<Node[]>([]);
  const [status, setStatus] = useState<Status>({ kind: 'idle', text: '' });
  const [menu, setMenu] = useState<{ x: number; y: number; at: XY } | null>(null);
  // ノードの右クリックのメニュー (screen は別ウィンドウを出す場所)
  const [nodeMenu, setNodeMenu] = useState<{ x: number; y: number; nodeId: string; screen: { x: number; y: number } } | null>(null);
  const viewer = useStudio((s) => s.viewer);
  const graphUnsaved = useStudio((s) => !!s.unsaved && (s.unsaved.graph || s.unsaved.pipelines.length > 0));
  const pipelineShaders = useStudio((s) => s.pipelineShaders);
  const shaderErrors = useStudio((s) => s.shaderErrors);
  const [newShader, setNewShader] = useState<{ pass: string; pipeline: string; stage: Stage } | null>(null);
  const wrapper = useRef<HTMLDivElement>(null);
  const fitted = useRef(false);

  const gpu = useMemo(() => {
    const m = new Map<string, number>();
    for (const t of stats?.passes ?? []) {
      const base = t.name.replace(/\[\d+\]$/, '');
      m.set(base, (m.get(base) ?? 0) + t.gpuMs);
    }
    return m;
  }, [stats]);

  const model = useMemo(() => (graph ? buildModel(graph.graph) : null), [graph]);
  const positions = useMemo(() => {
    if (!graph || !model) return {};
    const saved = savedPositions(graph.graph);
    const missing = model.nodes.some((n) => !saved[n.id]);
    return missing ? autoLayout(model, saved, true) : saved;
  }, [graph, model]);

  // グラフが変わったらノードを作り直す (ドラッグ中の位置は、離したときに保存してから戻ってくる)
  useEffect(() => {
    if (!graph || !model) return;
    const rt = runtimeOf(graph, gpu);
    // パイプラインのシェーダー (PS / CS) とコンパイルのエラー
    for (const n of model.nodes) {
      if (n.data.kind !== 'pass' || !n.data.pipeline) continue;
      const s = pipelineShaders[n.data.pipeline];
      if (!s) continue;
      let r = rt.get(n.id);
      if (!r) rt.set(n.id, (r = { errors: [], warnings: [] }));
      const main = s.ps ?? s.cs;
      if (main) r.shader = fileName(main);
      r.shaderErrors = shaderErrorsOf(n.data.pipeline, pipelineShaders, shaderErrors).map(fileName);
    }
    const selectedId = selection?.kind === 'pass' ? `pass:${selection.name}` : selection?.kind === 'resource' ? `res:${selection.name}` : '';
    setNodes(
      model.nodes.map((n) => ({
        id: n.id,
        type: n.data.kind === 'pass' ? 'pass' : 'resource',
        position: { x: positions[n.id]?.[0] ?? 0, y: positions[n.id]?.[1] ?? 0 },
        data: { ...n.data, runtime: rt.get(n.id) ?? { errors: [], warnings: [] } },
        selected: n.id === selectedId,
        width: n.width,
      })),
    );
  }, [graph, model, positions, gpu, selection, pipelineShaders, shaderErrors]);

  useEffect(() => {
    if (!fitted.current && nodes.length > 0) {
      fitted.current = true;
      requestAnimationFrame(() => flow.fitView({ padding: 0.15 }));
    }
  }, [nodes.length, flow]);

  const edges: Edge[] = useMemo(
    () =>
      (model?.edges ?? []).map((e) => ({
        id: e.id,
        source: e.source,
        sourceHandle: e.sourceHandle,
        target: e.target,
        targetHandle: e.targetHandle,
        label: e.label,
        className: e.write ? 'rf-edge write' : 'rf-edge read',
        labelBgPadding: [4, 2] as [number, number],
        labelBgBorderRadius: 3,
      })),
    [model],
  );

  const apply = useCallback(async (next: JsonObject, label: string) => {
    setStatus({ kind: 'busy', text: `${label}…` });
    const err = await applyGraph(next, label);
    setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: label });
    return !err;
  }, []);

  const onNodesChange = useCallback((changes: NodeChange[]) => setNodes((ns) => applyNodeChanges(changes, ns)), []);

  // 離したら位置を保存する
  const onNodeDragStop = useCallback(
    (_: unknown, __: Node, dragged: Node[]) => {
      if (!graph) return;
      const next = { ...savedPositions(graph.graph), ...positions };
      for (const n of nodes) next[n.id] = [n.position.x, n.position.y];
      for (const n of dragged) next[n.id] = [n.position.x, n.position.y];
      void apply(withPositions(graph.graph, next), dragged.length > 1 ? `${dragged.length} 個のノードを移動` : `${dragged[0]?.id.replace(/^\w+:/, '')} を移動`);
    },
    [graph, nodes, positions, apply],
  );

  const isValidConnection: IsValidConnection = useCallback((c) => {
    const s = parseNodeId(c.source);
    const t = parseNodeId(c.target);
    return !!s && !!t && s.kind !== t.kind;
  }, []);

  // 「出力 +」を何もない所で離したら、そこに新しいテクスチャを作って出力にする
  const onConnectEnd = useCallback(
    (e: MouseEvent | TouchEvent, state: FinalConnectionState) => {
      if (!graph || state.isValid || state.toNode || state.fromHandle?.id !== 'out:new' || !state.fromNode) return;
      const n = parseNodeId(state.fromNode.id);
      if (n?.kind !== 'pass') return;
      const point = 'changedTouches' in e ? e.changedTouches[0] : e;
      const p = flow.screenToFlowPosition({ x: point.clientX, y: point.clientY });
      const r = addOutput(graph.graph, n.name, [p.x, p.y - 20]);
      if ('error' in r) return setStatus({ kind: 'error', text: r.error });
      void apply(r.graph, r.label).then((ok) => ok && studioStore.select({ kind: 'resource', name: r.resource }));
    },
    [graph, apply, flow],
  );

  const onConnect = useCallback(
    (c: Connection) => {
      if (!graph) return;
      const r = connect(graph.graph, c.source, c.sourceHandle ?? '', c.target, c.targetHandle ?? '');
      if ('error' in r) return setStatus({ kind: 'error', text: r.error });
      const label = `つなぐ: ${r.label}`;
      // 新しい入力 (「+ 入力」へつないだ) は、パイプラインのテーブルが足りなければ広げてから送る
      const t = parseNodeId(c.target);
      if (t?.kind === 'pass' && c.targetHandle === 'in:new') {
        setStatus({ kind: 'busy', text: `${label}…` });
        void applyInputGraph(r.graph, t.name, label).then((err) => setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: label }));
        return;
      }
      void apply(r.graph, label);
    },
    [graph, apply],
  );

  // Delete / Backspace で消す (ノードを消すと、つながっている参照も消える)
  const onDelete = useCallback(
    ({ nodes: dn, edges: de }: { nodes: Node[]; edges: Edge[] }) => {
      if (!graph) return;
      if (dn.length > 0) {
        void apply(removeNodes(graph.graph, dn.map((n) => n.id)), `削除: ${dn.map((n) => n.id.replace(/^\w+:/, '')).join(', ')}`);
        studioStore.select(null);
      } else if (de.length > 0) void apply(removeEdges(graph.graph, de.map((e) => e.id)), `線を削除 (${de.length})`);
    },
    [graph, apply],
  );

  const onNodeClick = useCallback((_: unknown, node: Node) => {
    const n = parseNodeId(node.id);
    if (n) studioStore.select({ kind: n.kind, name: n.name });
  }, []);

  const relayout = useCallback(() => {
    if (!graph || !model) return;
    void apply(withPositions(graph.graph, autoLayout(model)), '自動配置').then(() => requestAnimationFrame(() => flow.fitView({ padding: 0.15 })));
  }, [graph, model, apply, flow]);

  // メニューの位置の基準 (メニューはキャンバスの中に置く)
  const canvasRect = () => (wrapper.current?.querySelector('.node-editor-canvas') ?? wrapper.current)?.getBoundingClientRect();

  // ---- 追加 (右クリックのメニュー、ヘッダーのボタン) ----
  const onPaneContextMenu = useCallback(
    (e: MouseEvent | React.MouseEvent) => {
      e.preventDefault();
      const rect = canvasRect();
      const p = flow.screenToFlowPosition({ x: e.clientX, y: e.clientY });
      setMenu({ x: e.clientX - (rect?.left ?? 0), y: e.clientY - (rect?.top ?? 0), at: [p.x, p.y] });
    },
    [flow],
  );

  // ---- ノードの結果を表示する (右クリックのメニュー) ----
  const onNodeContextMenu = useCallback((e: React.MouseEvent, node: Node) => {
    e.preventDefault();
    const rect = canvasRect();
    setMenu(null);
    setNodeMenu({ x: e.clientX - (rect?.left ?? 0), y: e.clientY - (rect?.top ?? 0), nodeId: node.id, screen: { x: e.screenX, y: e.screenY } });
    const n = parseNodeId(node.id);
    if (n) studioStore.select({ kind: n.kind, name: n.name });
  }, []);

  const showInViewport = (t: PreviewTarget) => {
    setNodeMenu(null);
    showInViewer(t.resource, t.format);
    if (!studioStore.get().viewportFloating) showPanel('viewport');
    setStatus({ kind: 'ok', text: `ビューポートに ${t.resource} を表示 (ツールバーの ✕ で最終出力に戻す)` });
  };
  const showInWindow = (t: PreviewTarget, at: { x: number; y: number }) => {
    setNodeMenu(null);
    void openPreview(t.resource, t.format, at).then((err) =>
      setStatus(err ? { kind: 'error', text: err } : { kind: 'ok', text: `${t.resource} を別ウィンドウに表示` }),
    );
  };
  const showInTab = (t: PreviewTarget) => {
    setNodeMenu(null);
    openPreviewTab(t.resource, t.format);
    setStatus({ kind: 'ok', text: `${t.resource} をタブに表示` });
  };
  // 新しいシェーダーを作ってパスにつなぐ: パイプラインを複製し、ステージを新しいシェーダーに差し替えて、パスのパイプラインにする
  const connectNewShader = async (pass: string, pipeline: string, stage: Stage, shaderPath: string) => {
    if (!graph) return;
    const base = shaderPath.slice(shaderPath.lastIndexOf('/') + 1).replace(/\.(vs|ps|cs)\.hlsl$/i, '').replace(/\.hlsl$/i, '');
    const newPipeline = `pipelines/${base}.json`;
    const src = (await call('asset.read', { path: pipeline })).text;
    await call('asset.write', { path: newPipeline, text: withShaderFile(src, stage, shaderPath), create: true });
    const next = JSON.parse(JSON.stringify(graph.graph)) as JsonObject;
    for (const p of (next.passes as JsonObject[]) ?? []) if (p.name === pass) p.pipeline = newPipeline;
    await apply(next, `${pass} を新しいシェーダー ${fileName(shaderPath)} に`);
    void loadPipelineShaders([...graph.pipelines, newPipeline]);
    openAssetTab(shaderPath);
  };

  const ROLE_LABEL: Record<PreviewTarget['role'], string> = { resource: '', output: '出力', input: '入力' };

  const centerOfView = (): XY => {
    const rect = canvasRect();
    const p = flow.screenToFlowPosition({ x: (rect?.left ?? 0) + (rect?.width ?? 400) / 2, y: (rect?.top ?? 0) + (rect?.height ?? 300) / 2 });
    return [p.x - 100, p.y - 40];
  };

  const doAddPass = (kind: PassKind, at: XY) => {
    setMenu(null);
    if (!graph) return;
    const pipeline = graph.pipelines.find((p) => (kind === 'Compute' ? /compute|cs/i.test(p) : !/compute|cs/i.test(p))) ?? graph.pipelines[0] ?? '';
    const r = addPass(graph.graph, kind, pipeline, at);
    void apply(r.graph, `パス ${r.name} を追加`).then((ok) => ok && studioStore.select({ kind: 'pass', name: r.name }));
  };
  // 描画ノード: 既定の VS / PS / パイプラインをテンプレートから作り、メッシュを描くパス (layer = ノードの名前) を加える
  const doAddDrawNode = async (at: XY) => {
    setMenu(null);
    if (!graph) return;
    try {
      const files = (await call('asset.list', { dir: '', extensions: ['.hlsl', '.json'] })).files;
      const n = drawNodeNames(graph.graph, files);
      setStatus({ kind: 'busy', text: `描画ノード ${n.name} のシェーダーとパイプラインを作っています…` });
      await call('asset.create', { template: 'shader.draw.vs', path: n.vs });
      await call('asset.create', { template: 'shader.draw.ps', path: n.ps });
      const target = drawNodeTarget(graph.graph);
      await call('asset.create', { template: target.depth ? 'pipeline.draw' : 'pipeline.draw.nodepth', path: n.pipeline, shader: n.ps, vs: n.vs });
      if (!(await apply(addDrawPass(graph.graph, n.name, n.pipeline, at), `描画ノード ${n.name} を追加`))) return;
      studioStore.select({ kind: 'pass', name: n.name });
      void loadPipelineShaders([...graph.pipelines, n.pipeline]);
      setStatus({
        kind: 'ok',
        text:
          `描画ノード ${n.name} を追加しました (${n.vs}、${n.ps}、${n.pipeline})。物体のインスペクターの描画レイヤーで ${n.name} を選ぶと、このノードで描きます` +
          (target.depth ? '' : '。sceneColor がないので $backbuffer に深度なしで描きます'),
      });
    } catch (e) {
      setStatus({ kind: 'error', text: e instanceof Error ? e.message : String(e) });
    }
  };
  const doAddResource = (kind: 'texture' | 'buffer', at: XY) => {
    setMenu(null);
    if (!graph) return;
    const r = addResource(graph.graph, kind, at);
    void apply(r.graph, `リソース ${r.name} を追加`).then((ok) => ok && studioStore.select({ kind: 'resource', name: r.name }));
  };

  if (!graph) return <div className="panel"><div className="placeholder">プロジェクトのグラフを待っています…</div></div>;

  return (
    <div
      className="panel node-editor"
      ref={wrapper}
      onClick={() => {
        if (menu) setMenu(null);
        if (nodeMenu) setNodeMenu(null);
      }}
    >
      <header className="panel-header">
        <span className="panel-title">ノードエディター</span>
        {graphUnsaved && (
          <span className="unsaved-mark" title="レンダーグラフ / パイプラインに保存していない変更があります (Ctrl+S ですべて保存)">
            ● 未保存
          </span>
        )}
        <span className="spacer" />
        <button onClick={() => void doAddDrawNode(centerOfView())} title="メッシュを描くノードを加える (既定の VS / PS / パイプラインも作る。描画レイヤーの名前はノードの名前)">
          ＋ 描画ノード
        </button>
        <button onClick={() => doAddPass('Raster', centerOfView())} title="Raster のパス (全画面) を加える">＋ パス</button>
        <button onClick={() => doAddResource('texture', centerOfView())} title="テクスチャを加える">＋ リソース</button>
        <button onClick={() => void saveAll()} title="レンダーグラフとパイプラインとシーンを保存する (Ctrl+S)">保存</button>
        <button onClick={relayout} title="パスとリソースを、書く → 読むの順に左から右へ並べ直す">自動配置</button>
        <button onClick={() => flow.fitView({ padding: 0.15 })}>全体を表示</button>
      </header>
      {status.text && <div className={`inspector-status ${status.kind}`} title={status.text}>{status.text.split('\n').slice(0, 3).join(' / ')}</div>}
      <div className="node-editor-canvas">
        <ReactFlow
          nodes={nodes}
          edges={edges}
          nodeTypes={nodeTypes}
          onNodesChange={onNodesChange}
          onNodeDragStop={onNodeDragStop}
          onConnect={onConnect}
          onConnectEnd={onConnectEnd}
          isValidConnection={isValidConnection}
          onDelete={onDelete}
          onNodeClick={onNodeClick}
          onPaneClick={() => {
            setMenu(null);
            setNodeMenu(null);
          }}
          onPaneContextMenu={(e) => {
            setNodeMenu(null);
            onPaneContextMenu(e);
          }}
          onNodeContextMenu={onNodeContextMenu}
          deleteKeyCode={['Delete', 'Backspace']}
          colorMode="dark"
          minZoom={0.2}
          maxZoom={2}
          proOptions={{ hideAttribution: true }}
        >
          <Background gap={20} size={1} />
          <Controls showInteractive={false} />
        </ReactFlow>
        {newShader && (
          <NewShaderDialog
            stage={newShader.stage}
            defaultName={`${newShader.pass.toLowerCase()}_custom`}
            title={`${newShader.pass} の新しいシェーダー`}
            note={`${fileName(newShader.pipeline)} を複製して新しいシェーダーに差し替え、${newShader.pass} につなぎます (つなぎ替えは元に戻せます)`}
            onCreated={(path) => connectNewShader(newShader.pass, newShader.pipeline, newShader.stage, path)}
            onClose={() => setNewShader(null)}
          />
        )}
        {menu && (
          <FloatingMenu className="rf-menu" x={menu.x} y={menu.y}>
            <div className="rf-menu-title">パスを追加</div>
            <button onClick={() => void doAddDrawNode(menu.at)} title="既定の VS / PS / パイプラインも作る">描画ノード (メッシュ)</button>
            <button onClick={() => doAddPass('Raster', menu.at)}>Raster (全画面)</button>
            <button onClick={() => doAddPass('Compute', menu.at)}>Compute</button>
            <button onClick={() => doAddPass('Copy', menu.at)}>Copy</button>
            <div className="rf-menu-title">リソースを追加</div>
            <button onClick={() => doAddResource('texture', menu.at)}>テクスチャ</button>
            <button onClick={() => doAddResource('buffer', menu.at)}>バッファ</button>
          </FloatingMenu>
        )}
        {nodeMenu && (
          <FloatingMenu className="rf-menu rf-node-menu" x={nodeMenu.x} y={nodeMenu.y}>
            <div className="rf-menu-title">{nodeMenu.nodeId.replace(/^\w+:/, '')} の結果を表示</div>
            {(() => {
              const targets = previewTargets(graph.graph, nodeMenu.nodeId);
              if (targets.length === 0) return <div className="rf-menu-note">表示できるテクスチャがありません (インポートとバッファは表示できません)</div>;
              return targets.map((t) => (
                <div key={t.resource} className="rf-menu-row">
                  <span className="rf-menu-label" title={t.format}>
                    {ROLE_LABEL[t.role] && <span className="rf-menu-role">{ROLE_LABEL[t.role]}</span>}
                    {t.resource}
                  </span>
                  <button onClick={() => showInViewport(t)} title="ビューポートに、最終出力の代わりにこのテクスチャを表示する">
                    ビューポート
                  </button>
                  <button onClick={() => showInTab(t)} title="このテクスチャをタブ (パネル) に表示する。タブはドラッグで並べ替え / ウィンドウの外へ出せる">
                    タブ
                  </button>
                  <button onClick={() => showInWindow(t, nodeMenu.screen)} title="このテクスチャを別ウィンドウに表示する">
                    別ウィンドウ
                  </button>
                </div>
              ));
            })()}
            {(() => {
              const n = parseNodeId(nodeMenu.nodeId);
              const pass = n?.kind === 'pass' ? ((graph.graph.passes as JsonObject[]) ?? []).find((p) => p.name === n.name) : undefined;
              if (!pass) return null;
              const pipeline = String(pass.pipeline ?? '');
              const s = pipeline ? pipelineShaders[pipeline] : undefined;
              const stage: Stage = pass.type === 'Compute' ? 'cs' : 'ps';
              return (
                <>
                  <div className="rf-menu-title">パイプライン / シェーダー</div>
                  {!pipeline && <div className="rf-menu-note">このパスはパイプラインを使いません</div>}
                  {pipeline && (
                    <button onClick={() => { setNodeMenu(null); openPipelineTab(pipeline); }}>
                      パイプラインを開く ({fileName(pipeline)})
                    </button>
                  )}
                  {s &&
                    (['vs', 'ps', 'cs'] as const)
                      .filter((st) => s[st])
                      .map((st) => (
                        <button key={st} onClick={() => { setNodeMenu(null); openAssetTab(s[st]!); }}>
                          {st.toUpperCase()} を開く ({fileName(s[st]!)}){shaderErrors[s[st]!] ? ' · エラー' : ''}
                        </button>
                      ))}
                  {pipeline && (
                    <button
                      onClick={() => {
                        setNodeMenu(null);
                        setNewShader({ pass: String(pass.name), pipeline, stage });
                      }}
                      title="テンプレートから新しいシェーダーを作り、パイプラインを複製して差し替え、このパスにつなぐ"
                    >
                      新しいシェーダーを作る…
                    </button>
                  )}
                </>
              );
            })()}
            {viewer?.enabled && (
              <button
                className="rf-menu-reset"
                onClick={() => {
                  setNodeMenu(null);
                  void setViewer({ enabled: false });
                }}
              >
                ビューポートを最終出力に戻す
              </button>
            )}
          </FloatingMenu>
        )}
      </div>
    </div>
  );
}

// 右クリックのメニュー。パネルの外にはみ出すなら、内側へずらす (下にはみ出せば上へ、右にはみ出せば左へ)
function FloatingMenu({ x, y, className, children }: { x: number; y: number; className: string; children: React.ReactNode }) {
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState({ left: x, top: y });
  useLayoutEffect(() => {
    const el = ref.current;
    const parent = el?.offsetParent as HTMLElement | null;
    if (!el || !parent) return setPos({ left: x, top: y });
    const maxLeft = parent.clientWidth - el.offsetWidth - 4;
    const maxTop = parent.clientHeight - el.offsetHeight - 4;
    setPos({ left: Math.max(4, Math.min(x, maxLeft)), top: Math.max(4, Math.min(y, maxTop)) });
  }, [x, y]);
  return (
    <div ref={ref} className={className} style={pos} onClick={(e) => e.stopPropagation()}>
      {children}
    </div>
  );
}
