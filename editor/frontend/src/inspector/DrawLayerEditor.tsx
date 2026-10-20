// ============================================================
//  インスペクター: 物体の描画レイヤー (どの描画ノードでこのメッシュを描くか)
//
//  物体のレイヤー (書いていなければ既定の Opaque) と、スロットごとの上書き。選べるのはグラフの描画ノード
//  (drawList) の layer。どのノードにも入らないメッシュはビューポートでピンクになる (state/layers.ts)。
// ============================================================
import type { SceneJson, SceneObjectJson } from '../bridge/protocol';
import {
  DEFAULT_DRAW_LAYER,
  type DrawLayerInfo,
  graphDrawLayers,
  isUndrawn,
  objectDrawLayers,
  toggleLayer,
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

export function DrawLayerEditor({ scene, obj, slots, apply }: Props) {
  const graph = useStudio((s) => s.graph);
  const available = graphDrawLayers(graph?.graph);
  const layers = objectDrawLayers(obj);
  const setObject = (next: string[] | undefined, label: string) => void apply(withObjectLayers(scene, obj.id, next), label);
  const setSlot = (slot: string, next: string[] | undefined, label: string) => void apply(withSlotLayers(scene, obj.id, slot, next), label);

  return (
    <>
      <div className="form-section muted" title="この物体のメッシュを描く描画ノード (グラフの drawList の layer)。影は「影を落とす」で決まる">
        描画レイヤー
      </div>
      <div className="form-row">
        <span className="form-label">物体</span>
        <span className="form-control draw-layers">
          <LayerChecks
            layers={layers}
            available={available}
            onChange={(next, label) => setObject(next, `描画レイヤー: ${label}`)}
          />
          {obj.layers === undefined ? (
            <span className="muted" title="layers を書いていない物体は既定のレイヤーで描く">(既定)</span>
          ) : (
            <button onClick={() => setObject(undefined, `描画レイヤーを既定 (${DEFAULT_DRAW_LAYER}) に戻す`)} title={`layers を消して既定 (${DEFAULT_DRAW_LAYER}) に戻す`}>
              既定に戻す
            </button>
          )}
        </span>
      </div>
      {isUndrawn(layers, available) && <UndrawnNote />}
      {slots.map((slot) => {
        const override = obj.slotLayers?.[slot];
        return (
          <div key={slot}>
            <div className="form-row">
              <span className="form-label" title={slot}>{slot}</span>
              <span className="form-control draw-layers">
                <select
                  value={override ? 'override' : 'object'}
                  onChange={(e) =>
                    e.target.value === 'override'
                      ? setSlot(slot, [...layers], `${slot} の描画レイヤーを上書き`)
                      : setSlot(slot, undefined, `${slot} の描画レイヤーを物体と同じに戻す`)
                  }
                  title="このスロット (サブメッシュ) だけ別の描画ノードで描く"
                >
                  <option value="object">物体と同じ</option>
                  <option value="override">上書きする</option>
                </select>
                {override && (
                  <LayerChecks layers={override} available={available} onChange={(next, label) => setSlot(slot, next, `${slot} の描画レイヤー: ${label}`)} />
                )}
              </span>
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

// グラフのレイヤーのチェック (グラフにないレイヤーも、外せるように出す)
function LayerChecks({
  layers,
  available,
  onChange,
}: {
  layers: string[];
  available: DrawLayerInfo[];
  onChange: (next: string[], label: string) => void;
}) {
  const missing = layers.filter((l) => !available.some((a) => a.layer === l));
  const item = (layer: string, title: string, extra = '') => (
    <label key={layer} className="draw-layer" title={title}>
      <input
        type="checkbox"
        checked={layers.includes(layer)}
        onChange={(e) => onChange(toggleLayer(layers, layer, e.target.checked, available), `${layer} ${e.target.checked ? 'に入れる' : 'から外す'}`)}
      />
      {layer}
      {extra && <span className="muted"> {extra}</span>}
    </label>
  );
  return (
    <span className="draw-layer-list">
      {available.map((a) => item(a.layer, `描くノード: ${a.passes.join(', ')}`))}
      {missing.map((l) => item(l, 'グラフにこのレイヤーを描くノードがありません', '(グラフにない)'))}
      {available.length === 0 && missing.length === 0 && <span className="muted">グラフに描画ノードがありません</span>}
    </span>
  );
}
