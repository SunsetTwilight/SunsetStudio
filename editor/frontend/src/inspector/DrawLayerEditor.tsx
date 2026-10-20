// ============================================================
//  インスペクター: 物体の描画レイヤー (どの描画ノードでこのメッシュを描くか)
//
//  物体のレイヤー (書いていなければ既定の Opaque) と、スロットごとの上書き。選べるのはグラフの描画ノード
//  (drawList) の layer。1 行に 1 つのドロップダウンで選び、横の「＋」で行を足す (2 つ以上のノードで描く)、
//  「×」で外す。最初の行には「既定」/「物体と同じ」/「なし」も出す。
//  どのノードにも入らないメッシュはビューポートでピンクになる (state/layers.ts)。
// ============================================================
import type { SceneJson, SceneObjectJson } from '../bridge/protocol';
import {
  DEFAULT_DRAW_LAYER,
  type DrawLayerInfo,
  graphDrawLayers,
  isUndrawn,
  nextUnusedLayer,
  objectDrawLayers,
  removeLayerAt,
  setLayerAt,
  withObjectLayers,
  withSlotLayers,
} from '../state/layers';
import { useStudio } from '../state/store';

interface Props {
  scene: SceneJson;
  obj: SceneObjectJson;
  slots: string[];
  apply: (next: SceneJson, label: string) => Promise<void>;
}

const DEFAULT = '__default';
const OBJECT = '__object';
const NONE = '__none';

export function DrawLayerEditor({ scene, obj, slots, apply }: Props) {
  const graph = useStudio((s) => s.graph);
  const available = graphDrawLayers(graph?.graph);
  const layers = objectDrawLayers(obj);
  const setObject = (next: string[] | undefined, label: string) => void apply(withObjectLayers(scene, obj.id, next), `描画レイヤー: ${label}`);
  const setSlot = (slot: string, next: string[] | undefined, label: string) =>
    void apply(withSlotLayers(scene, obj.id, slot, next), `${slot} の描画レイヤー: ${label}`);

  return (
    <>
      <div className="form-section muted" title="この物体のメッシュを描く描画ノード (グラフの drawList の layer)。影は「影を落とす」で決まる">
        描画レイヤー
      </div>
      <div className="form-row">
        <span className="form-label">物体</span>
        <LayerRows
          layers={obj.layers ?? null}
          effective={layers}
          special={obj.layers === undefined ? DEFAULT : NONE}
          specials={[
            { value: DEFAULT, label: `既定 (${DEFAULT_DRAW_LAYER})` },
            { value: NONE, label: '(なし: どこにも描かない)' },
          ]}
          available={available}
          onSpecial={(v) => (v === DEFAULT ? setObject(undefined, `既定 (${DEFAULT_DRAW_LAYER}) に戻す`) : setObject([], 'なし'))}
          onLayers={setObject}
        />
      </div>
      {isUndrawn(layers, available) && <UndrawnNote />}
      {slots.map((slot) => {
        const override = obj.slotLayers?.[slot];
        return (
          <div key={slot}>
            <div className="form-row">
              <span className="form-label" title={`${slot}: このスロット (サブメッシュ) だけ別の描画ノードで描く`}>{slot}</span>
              <LayerRows
                layers={override ?? null}
                effective={override ?? layers}
                special={override === undefined ? OBJECT : NONE}
                specials={[
                  { value: OBJECT, label: '物体と同じ' },
                  { value: NONE, label: '(なし: どこにも描かない)' },
                ]}
                available={available}
                onSpecial={(v) => (v === OBJECT ? setSlot(slot, undefined, '物体と同じに戻す') : setSlot(slot, [], 'なし'))}
                onLayers={(next, label) => setSlot(slot, next, label)}
              />
            </div>
            {override && isUndrawn(override, available) && <UndrawnNote />}
          </div>
        );
      })}
    </>
  );
}

function UndrawnNote() {
  return <div className="inspector-status error">どの描画ノードにも入りません (ビューポートではピンクで表示)</div>;
}

interface RowsProps {
  layers: string[] | null; // null = 指定なし (既定 / 物体と同じ)
  effective: string[]; // 実際に描くレイヤー (「＋」はこれに足す)
  special: string; // 指定なし、または空のときに最初の行で選んでいるもの
  specials: { value: string; label: string }[];
  available: DrawLayerInfo[];
  onSpecial: (value: string) => void;
  onLayers: (next: string[], label: string) => void;
}

// 1 行に 1 つのドロップダウン。最後の行の横に「＋」、2 行以上なら各行に「×」 (ない所は空けて、行の幅をそろえる)
function LayerRows({ layers, effective, special, specials, available, onSpecial, onLayers }: RowsProps) {
  const next = nextUnusedLayer(effective, available);
  const add = (
    <button
      className="draw-layer-add"
      disabled={!next}
      onClick={() => next && onLayers([...effective, next], `${next} を足す`)}
      title={next ? `描画ノードを足す (${next})` : '足せるレイヤーがありません'}
    >
      ＋
    </button>
  );
  const option = (a: DrawLayerInfo) => (
    <option key={a.layer} value={a.layer} title={`描くノード: ${a.passes.join(', ')}`}>
      {a.passes.length === 1 && a.passes[0] === a.layer ? a.layer : `${a.layer} (${a.passes.join(', ')})`}
    </option>
  );

  // 指定なし / 空: 最初の行だけ
  if (!layers || layers.length === 0) {
    return (
      <span className="form-control draw-layers">
        <span className="draw-layer-row">
          <select value={special} onChange={(e) => (specials.some((s) => s.value === e.target.value) ? onSpecial(e.target.value) : onLayers([e.target.value], e.target.value))}>
            {specials.map((s) => (
              <option key={s.value} value={s.value}>
                {s.label}
              </option>
            ))}
            {available.map(option)}
          </select>
          {add}
          <span className="draw-layer-spacer" />
        </span>
      </span>
    );
  }

  return (
    <span className="form-control draw-layers">
      {layers.map((layer, i) => {
        const known = available.some((a) => a.layer === layer);
        return (
          <span className="draw-layer-row" key={`${i}:${layer}`}>
            <select
              value={layer}
              onChange={(e) =>
                specials.some((s) => s.value === e.target.value)
                  ? onSpecial(e.target.value)
                  : onLayers(setLayerAt(layers, i, e.target.value), `${layer} → ${e.target.value}`)
              }
            >
              {i === 0 &&
                specials.map((s) => (
                  <option key={s.value} value={s.value}>
                    {s.label}
                  </option>
                ))}
              {available.filter((a) => a.layer === layer || !layers.includes(a.layer)).map(option)}
              {!known && <option value={layer}>{layer} (グラフにない)</option>}
            </select>
            {i === layers.length - 1 ? add : <span className="draw-layer-spacer" />}
            {layers.length > 1 ? (
              <button className="draw-layer-remove" onClick={() => onLayers(removeLayerAt(layers, i), `${layer} を外す`)} title={`${layer} を外す`}>
                ×
              </button>
            ) : (
              <span className="draw-layer-spacer" />
            )}
          </span>
        );
      })}
    </span>
  );
}
