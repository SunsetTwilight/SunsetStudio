// ============================================================
//  ノードエディターのパネル (M4): ビューポートのレンダーグラフを、ノードと線で編集する
//
//    表示   パス (入力 / 出力、パイプライン、実行順、GPU 時間、実行しない理由、診断) とリソース (フォーマット、大きさ)。
//           線は 書く (パス → リソース、橙) と 読む (リソース → パス、灰)。ラベルは使い方 (RTV0 / SRV など)
//    編集   ドラッグで移動 (位置はグラフの editor に保存)、ハンドルどうしをつなぐ (入力 / 出力を加える・差し替える)、
//           Delete で線 / ノードを消す、右クリックでパス / リソースを加える、クリックでインスペクターに表示
//    結果   ノードを右クリック → そのノードの結果 (リソースならそれ自身、パスなら出力 / 入力のテクスチャ) だけを
//           ビューポートに表示する (リソースビューアー)、タブに表示する、または別ウィンドウに表示する (preview.open)
//    操作   自動配置 (dagre)、全体を表示
//  編集はすべてグラフ全体の JSON を graph.set で送る (コンパイルできなければエラーで前のまま、元に戻す / やり直すが効く)。
// ============================================================
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  Background,
  Controls,
  ReactFlow,
  ReactFlowProvider,
  applyNodeChanges,
  useReactFlow,
  type Connection,
  type Edge,
  type IsValidConnection,
  type Node,
  type NodeChange,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { isConnected } from '../bridge/client';
import type { GraphInfo, JsonObject } from '../bridge/protocol';
import { setViewer, showInViewer, openPreview } from '../panels/ViewerPanel';
import { openPreviewTab, showPanel } from '../state/dock';
import { applyGraph } from '../state/graph';
import { studioStore, useStudio } from '../state/store';
import {
  addPass,
  addResource,
  autoLayout,
  buildModel,
  connect,
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
  }, [graph, model, positions, gpu, selection]);

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

  const onConnect = useCallback(
    (c: Connection) => {
      if (!graph) return;
      const r = connect(graph.graph, c.source, c.sourceHandle ?? '', c.target, c.targetHandle ?? '');
      if ('error' in r) return setStatus({ kind: 'error', text: r.error });
      void apply(r.graph, `つなぐ: ${r.label}`);
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

  // ---- 追加 (右クリックのメニュー、ヘッダーのボタン) ----
  const onPaneContextMenu = useCallback(
    (e: MouseEvent | React.MouseEvent) => {
      e.preventDefault();
      const rect = wrapper.current?.getBoundingClientRect();
      const p = flow.screenToFlowPosition({ x: e.clientX, y: e.clientY });
      setMenu({ x: e.clientX - (rect?.left ?? 0), y: e.clientY - (rect?.top ?? 0), at: [p.x, p.y] });
    },
    [flow],
  );

  // ---- ノードの結果を表示する (右クリックのメニュー) ----
  const onNodeContextMenu = useCallback((e: React.MouseEvent, node: Node) => {
    e.preventDefault();
    const rect = wrapper.current?.getBoundingClientRect();
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
  const ROLE_LABEL: Record<PreviewTarget['role'], string> = { resource: '', output: '出力', input: '入力' };

  const centerOfView = (): XY => {
    const rect = wrapper.current?.getBoundingClientRect();
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
        <span className="spacer" />
        <button onClick={() => doAddPass('Raster', centerOfView())} title="Raster のパス (全画面) を加える">＋ パス</button>
        <button onClick={() => doAddResource('texture', centerOfView())} title="テクスチャを加える">＋ リソース</button>
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
        {menu && (
          <div className="rf-menu" style={{ left: menu.x, top: menu.y }} onClick={(e) => e.stopPropagation()}>
            <div className="rf-menu-title">パスを追加</div>
            <button onClick={() => doAddPass('Raster', menu.at)}>Raster (全画面)</button>
            <button onClick={() => doAddPass('Compute', menu.at)}>Compute</button>
            <button onClick={() => doAddPass('Copy', menu.at)}>Copy</button>
            <div className="rf-menu-title">リソースを追加</div>
            <button onClick={() => doAddResource('texture', menu.at)}>テクスチャ</button>
            <button onClick={() => doAddResource('buffer', menu.at)}>バッファ</button>
          </div>
        )}
        {nodeMenu && (
          <div className="rf-menu rf-node-menu" style={{ left: nodeMenu.x, top: nodeMenu.y }} onClick={(e) => e.stopPropagation()}>
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
          </div>
        )}
      </div>
    </div>
  );
}
