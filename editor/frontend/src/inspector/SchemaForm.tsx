// ============================================================
//  スキーマから作るフォーム
//
//  エンジンのフィールド表 (schema.describe) をたどって、記述子の値 (JSON) を編集するフォームを作る。
//  構造体ごとの手書きの UI は作らない (エンジンに構造体やフィールドを足せば、フォームにもそのまま出る)。
//
//    bool → チェックボックス、enum → 選択、flags → ビットごとのチェック、数値 → 入力欄 (Enter / フォーカスを外すと確定)、
//    固定長の文字列 → 入力欄、構造体 → 折りたためるグループ、固定長配列 → 要素ごとの行 / グループ
//  既定値と違うフィールドは名前を強調する。tooltip は対応する D3D12 のメンバー。
// ============================================================
import { useEffect, useState } from 'react';
import type { EnumDesc, FieldDesc, JsonObject, JsonValue, TypeDescription } from '../bridge/protocol';

export type Path = (string | number)[];

interface Props {
  schema: TypeDescription;
  value: JsonObject;
  filter: string;
  onChange: (path: Path, value: JsonValue) => void;
}

export function SchemaForm({ schema, value, filter, onChange }: Props) {
  const root = schema.structs[schema.type];
  if (!root) return <div className="form-error">型 {schema.type} の説明がありません</div>;
  const q = filter.trim().toLowerCase();
  return (
    <div className="schema-form">
      {q ? (
        <FlatFields schema={schema} structName={schema.type} value={value} path={[]} filter={q} onChange={onChange} />
      ) : (
        <StructFields schema={schema} structName={schema.type} value={value} path={[]} onChange={onChange} depth={0} />
      )}
    </div>
  );
}

// ---- 構造体のフィールド (入れ子) ----
function StructFields(props: {
  schema: TypeDescription;
  structName: string;
  value: JsonValue | undefined;
  path: Path;
  depth: number;
  onChange: Props['onChange'];
}) {
  const { schema, structName, value, path, depth, onChange } = props;
  const sd = schema.structs[structName];
  if (!sd) return null;
  const obj = (value && typeof value === 'object' && !Array.isArray(value) ? value : {}) as JsonObject;
  return (
    <>
      {sd.fields.map((f) => (
        <Field key={f.name} schema={schema} field={f} value={obj[f.name]} path={[...path, f.name]} depth={depth} onChange={onChange} />
      ))}
    </>
  );
}

function Field(props: {
  schema: TypeDescription;
  field: FieldDesc;
  value: JsonValue | undefined;
  path: Path;
  depth: number;
  onChange: Props['onChange'];
}) {
  const { schema, field, value, path, depth, onChange } = props;
  if (field.name === 'structSize') return null;

  // 固定長配列 (文字列は 1 つの値として扱う)
  if (field.count > 1 && field.type !== 'string') {
    const items = Array.isArray(value) ? value : [];
    return (
      <details className="form-group" open={depth === 0 && field.type !== 'struct'}>
        <summary title={field.tooltip}>
          {field.name} <span className="muted">[{field.count}]</span>
        </summary>
        <div className="form-group-body">
          {Array.from({ length: field.count }, (_, i) =>
            field.type === 'struct' ? (
              <details key={i} className="form-group" open={i === 0}>
                <summary>
                  {field.name}[{i}]
                </summary>
                <div className="form-group-body">
                  <StructFields schema={schema} structName={field.struct!} value={items[i]} path={[...path, i]} depth={depth + 1} onChange={onChange} />
                </div>
              </details>
            ) : (
              <Row key={i} label={`[${i}]`} tooltip={field.tooltip} modified={false}>
                <ScalarInput schema={schema} field={field} value={items[i]} onCommit={(v) => onChange([...path, i], v)} />
              </Row>
            ),
          )}
        </div>
      </details>
    );
  }

  if (field.type === 'struct') {
    return (
      <details className="form-group" open={depth === 0}>
        <summary title={field.tooltip}>{field.name}</summary>
        <div className="form-group-body">
          <StructFields schema={schema} structName={field.struct!} value={value} path={path} depth={depth + 1} onChange={onChange} />
        </div>
      </details>
    );
  }

  if (!field.serialized) {
    return (
      <Row label={field.name} tooltip={field.tooltip} modified={false}>
        <span className="muted">{field.type === 'handle' ? '(ハンドル: 編集できません)' : '(編集できません)'}</span>
      </Row>
    );
  }

  const modified = field.default !== undefined && JSON.stringify(field.default) !== JSON.stringify(value);
  return (
    <Row label={field.name} tooltip={field.tooltip} modified={modified}>
      <ScalarInput schema={schema} field={field} value={value} onCommit={(v) => onChange(path, v)} />
    </Row>
  );
}

// ---- 絞り込み: 名前が一致する末端のフィールドだけを、パス付きで並べる ----
function FlatFields(props: {
  schema: TypeDescription;
  structName: string;
  value: JsonValue | undefined;
  path: Path;
  filter: string;
  onChange: Props['onChange'];
}) {
  const rows: React.ReactNode[] = [];
  const walk = (structName: string, value: JsonValue | undefined, path: Path) => {
    const sd = props.schema.structs[structName];
    if (!sd) return;
    const obj = (value && typeof value === 'object' && !Array.isArray(value) ? value : {}) as JsonObject;
    for (const f of sd.fields) {
      if (f.name === 'structSize' || !f.serialized) continue;
      const v = obj[f.name];
      const p = [...path, f.name];
      if (f.count > 1 && f.type !== 'string') {
        const items = Array.isArray(v) ? v : [];
        for (let i = 0; i < f.count; i++) {
          if (f.type === 'struct') walk(f.struct!, items[i], [...p, i]);
          else pushRow(f, items[i], [...p, i]);
        }
      } else if (f.type === 'struct') walk(f.struct!, v, p);
      else pushRow(f, v, p);
    }
  };
  const pushRow = (f: FieldDesc, v: JsonValue | undefined, p: Path) => {
    const label = formatPath(p);
    if (!label.toLowerCase().includes(props.filter)) return;
    const modified = f.default !== undefined && JSON.stringify(f.default) !== JSON.stringify(v);
    rows.push(
      <Row key={label} label={label} tooltip={f.tooltip} modified={modified}>
        <ScalarInput schema={props.schema} field={f} value={v} onCommit={(nv) => props.onChange(p, nv)} />
      </Row>,
    );
  };
  walk(props.structName, props.value, props.path);
  return rows.length ? <>{rows}</> : <div className="muted form-empty">一致するフィールドはありません</div>;
}

export function formatPath(path: Path): string {
  return path.map((p, i) => (typeof p === 'number' ? `[${p}]` : i === 0 ? p : `.${p}`)).join('');
}

function Row({ label, tooltip, modified, children }: { label: string; tooltip?: string; modified: boolean; children: React.ReactNode }) {
  return (
    <div className="form-row" title={tooltip}>
      <span className={modified ? 'form-label modified' : 'form-label'}>{label}</span>
      <span className="form-control">{children}</span>
    </div>
  );
}

// ---- 1 つの値の入力 ----
function ScalarInput({
  schema,
  field,
  value,
  onCommit,
}: {
  schema: TypeDescription;
  field: FieldDesc;
  value: JsonValue | undefined;
  onCommit: (v: JsonValue) => void;
}) {
  switch (field.type) {
    case 'bool':
      return <input type="checkbox" checked={value === true} onChange={(e) => onCommit(e.target.checked)} />;
    case 'enum': {
      const ed = field.enum ? schema.enums[field.enum] : undefined;
      return <EnumSelect desc={ed} value={value} onCommit={onCommit} />;
    }
    case 'flags': {
      const ed = field.enum ? schema.enums[field.enum] : undefined;
      return <FlagsInput desc={ed} value={value} onCommit={onCommit} />;
    }
    case 'string':
      return <TextInput value={typeof value === 'string' ? value : ''} maxLength={field.count - 1} onCommit={onCommit} />;
    case 'float':
      return <NumberInput value={value} integer={false} field={field} onCommit={onCommit} />;
    case 'uint8':
    case 'int32':
    case 'uint32':
      return <NumberInput value={value} integer field={field} onCommit={onCommit} />;
    default:
      return <span className="muted">{JSON.stringify(value)}</span>;
  }
}

function EnumSelect({ desc, value, onCommit }: { desc?: EnumDesc; value: JsonValue | undefined; onCommit: (v: JsonValue) => void }) {
  if (!desc) return <span className="muted">{String(value)}</span>;
  const current = typeof value === 'string' ? value : String(value ?? '');
  const known = desc.values.some((v) => v.name === current);
  return (
    <select value={current} onChange={(e) => onCommit(e.target.value)}>
      {!known && <option value={current}>{current}</option>}
      {desc.values.map((v) => (
        <option key={v.name} value={v.name}>
          {v.name}
        </option>
      ))}
    </select>
  );
}

function popcount(v: number): number {
  let n = 0;
  for (let x = v >>> 0; x; x &= x - 1) n++;
  return n;
}

// フラグは 1 ビットの列挙子ごとにチェックボックスを出す。書き戻すときは立っているビットの名前の配列
function FlagsInput({ desc, value, onCommit }: { desc?: EnumDesc; value: JsonValue | undefined; onCommit: (v: JsonValue) => void }) {
  if (!desc) return <span className="muted">{JSON.stringify(value)}</span>;
  const toMask = (v: JsonValue | undefined): number => {
    if (typeof v === 'number') return v >>> 0;
    if (Array.isArray(v)) return v.reduce<number>((m, e) => (m | toMask(e)) >>> 0, 0);
    if (typeof v === 'string') return (desc.values.find((d) => d.name === v)?.value ?? 0) >>> 0;
    return 0;
  };
  const mask = toMask(value);
  const bits = desc.values.filter((v) => popcount(v.value) === 1);
  const toggle = (bit: number, on: boolean) => {
    const next = on ? (mask | bit) >>> 0 : (mask & ~bit) >>> 0;
    const names: JsonValue[] = bits.filter((b) => (next & b.value) !== 0).map((b) => b.name);
    const rest = bits.reduce((m, b) => (m & ~b.value) >>> 0, next);
    if (rest) names.push(rest);
    onCommit(names);
  };
  return (
    <span className="flags">
      {bits.map((b) => (
        <label key={b.name} className="flag">
          <input type="checkbox" checked={(mask & b.value) !== 0} onChange={(e) => toggle(b.value, e.target.checked)} />
          {b.name}
        </label>
      ))}
    </span>
  );
}

// 入力中は送らず、Enter かフォーカスを外したときに確定する
function NumberInput({
  value,
  integer,
  field,
  onCommit,
}: {
  value: JsonValue | undefined;
  integer: boolean;
  field: FieldDesc;
  onCommit: (v: JsonValue) => void;
}) {
  const shown = typeof value === 'number' ? String(value) : typeof value === 'string' ? value : '';
  const [text, setText] = useState(shown);
  useEffect(() => setText(shown), [shown]);
  const commit = () => {
    if (text === shown) return;
    const t = text.trim();
    if (!integer && (t === 'inf' || t === '-inf' || t === 'nan')) return onCommit(t);
    const n = Number(t);
    if (t === '' || Number.isNaN(n)) {
      setText(shown); // 数値でなければ元に戻す
      return;
    }
    onCommit(integer ? Math.trunc(n) : n);
  };
  return (
    <input
      className="number"
      value={text}
      inputMode={integer ? 'numeric' : 'decimal'}
      placeholder={field.min !== undefined ? `${field.min}〜${field.max}` : ''}
      onChange={(e) => setText(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => {
        if (e.key === 'Enter') commit();
        if (e.key === 'Escape') setText(shown);
      }}
    />
  );
}

function TextInput({ value, maxLength, onCommit }: { value: string; maxLength: number; onCommit: (v: JsonValue) => void }) {
  const [text, setText] = useState(value);
  useEffect(() => setText(value), [value]);
  return (
    <input
      value={text}
      maxLength={maxLength}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => text !== value && onCommit(text)}
      onKeyDown={(e) => e.key === 'Enter' && text !== value && onCommit(text)}
    />
  );
}
