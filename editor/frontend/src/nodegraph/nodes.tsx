// ============================================================
//  ノードエディターのノード (React Flow のカスタムノード)
//
//    PassNode      パス。左に入力 (in:<番号>)、右に出力 (out:<番号>)、最後の行に新しい入力 / 出力 (+)
//    ResourceNode  リソース。左 (in) に書くパス、右 (out) から読むパス
//  実行の情報 (GPU 時間、実行しない理由、コンパイルの診断) は data の runtime に入れて渡す。
// ============================================================
import { Handle, Position, type NodeProps } from '@xyflow/react';
import type { PassNodeData, RefInfo, ResourceNodeData } from './model';

export interface NodeRuntime {
  gpuMs?: number;
  skipped?: string; // 実行しない理由 (表示用の文)
  order?: number; // 実行順 (1 から)
  errors: string[];
  warnings: string[];
}

export type PassFlowData = PassNodeData & { runtime: NodeRuntime };
export type ResourceFlowData = ResourceNodeData & { runtime: NodeRuntime };

const TYPE_BADGE: Record<string, string> = { Raster: 'R', Compute: 'C', Copy: 'Cp' };

function baseName(path: string) {
  const i = path.lastIndexOf('/');
  return (i >= 0 ? path.slice(i + 1) : path).replace(/\.json$/, '');
}

// 行に出す使い方 (bind があれば bind も。load は tooltip に出す)
function refLabel(r: RefInfo) {
  return r.bind ? `${r.as} ${r.bind}` : r.as;
}
function refTitle(r: RefInfo) {
  return [r.res, r.as, r.bind ? `bind ${r.bind}` : '', r.load ? `load ${r.load}` : ''].filter(Boolean).join(' · ');
}

function diagTitle(rt: NodeRuntime) {
  return [...rt.errors.map((e) => `エラー: ${e}`), ...rt.warnings.map((w) => `警告: ${w}`)].join('\n');
}

export function PassNode({ data, selected }: NodeProps & { data: PassFlowData }) {
  const rt = data.runtime;
  const rows = Math.max(data.inputs.length, data.outputs.length);
  const cls = ['rf-node', 'rf-pass', selected ? 'selected' : '', rt.skipped ? 'skipped' : '', rt.errors.length ? 'has-error' : rt.warnings.length ? 'has-warning' : '']
    .filter(Boolean)
    .join(' ');
  return (
    <div className={cls} title={diagTitle(rt) || undefined}>
      <div className="rf-head">
        <span className={`rf-badge type-${data.type}`}>{TYPE_BADGE[data.type] ?? data.type}</span>
        <span className="rf-name">{data.name}</span>
        {rt.order !== undefined && <span className="rf-order">#{rt.order}</span>}
        <span className="rf-ms">{rt.skipped ? '' : rt.gpuMs !== undefined ? `${rt.gpuMs.toFixed(3)} ms` : ''}</span>
      </div>
      <div className="rf-sub">{rt.skipped ? rt.skipped : data.pipeline ? baseName(data.pipeline) : data.type === 'Copy' ? 'コピー' : '(パイプラインなし)'}</div>
      <div className="rf-rows">
        {Array.from({ length: rows }, (_, i) => {
          const input = data.inputs[i];
          const output = data.outputs[i];
          return (
            <div key={i} className="rf-row">
              <span className="rf-in" title={input ? refTitle(input) : undefined}>
                {input && (
                  <>
                    <Handle type="target" position={Position.Left} id={`in:${i}`} className="rf-handle read" />
                    <span className="rf-ref">{input.res}</span>
                    <span className="rf-as">{refLabel(input)}</span>
                  </>
                )}
              </span>
              <span className="rf-out" title={output ? refTitle(output) : undefined}>
                {output && (
                  <>
                    <span className="rf-as">{refLabel(output)}</span>
                    <span className="rf-ref">{output.res}</span>
                    <Handle type="source" position={Position.Right} id={`out:${i}`} className="rf-handle write" />
                  </>
                )}
              </span>
            </div>
          );
        })}
        <div className="rf-row rf-new">
          <span className="rf-in" title="リソースの右 (読む) からここへつなぐと、入力を加える">
            <Handle type="target" position={Position.Left} id="in:new" className="rf-handle new" />+ 入力
          </span>
          <span className="rf-out" title="ここからリソースの左 (書く) へつなぐと、出力を加える">
            出力 +
            <Handle type="source" position={Position.Right} id="out:new" className="rf-handle new" />
          </span>
        </div>
      </div>
    </div>
  );
}

export function ResourceNode({ data, selected }: NodeProps & { data: ResourceFlowData }) {
  const rt = data.runtime;
  const cls = ['rf-node', 'rf-res', `kind-${data.resKind}`, selected ? 'selected' : '', rt.skipped ? 'skipped' : '', rt.errors.length ? 'has-error' : rt.warnings.length ? 'has-warning' : '']
    .filter(Boolean)
    .join(' ');
  return (
    <div className={cls} title={diagTitle(rt) || undefined}>
      <Handle type="target" position={Position.Left} id="in" className="rf-handle write" />
      <div className="rf-head">
        <span className="rf-name">{data.name}</span>
      </div>
      <div className="rf-sub">
        {data.format} {data.size}
        {rt.skipped ? ` · ${rt.skipped}` : ''}
      </div>
      <Handle type="source" position={Position.Right} id="out" className="rf-handle read" />
    </div>
  );
}

export const nodeTypes = { pass: PassNode, resource: ResourceNode };
