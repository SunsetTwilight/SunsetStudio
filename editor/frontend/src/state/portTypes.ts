// ============================================================
//  パスの入力 / 出力の型 (シェーダーの変数の型) とフォーマット (graph.portTypes)
//
//    loadPortTypes()  すべてのパスの分を読んでストアに入れる (グラフ、シェーダー、パイプラインが変わったとき)
//    shortType()      ノードに出す短い型 ("Texture2D<float>" → "float"、"Texture2D" → "float4")
//    shortFormat()    ノードに出す短いフォーマット ("R16G16B16A16_Float" → "RGBA16F")
// ============================================================
import { call } from '../bridge/client';
import { studioStore } from './store';

export async function loadPortTypes(): Promise<void> {
  try {
    studioStore.setPortTypes(await call('graph.portTypes'));
  } catch {
    studioStore.setPortTypes(null);
  }
}

export function shortType(type: string | undefined): string {
  if (!type) return '';
  const m = /<\s*([^>]+)\s*>/.exec(type);
  if (m) return m[1].trim();
  if (/Texture|Buffer/.test(type)) return 'float4';
  return type;
}

const SUFFIX: Record<string, string> = { Float: 'F', Unorm: '', Snorm: 'SN', Uint: 'UI', Sint: 'I', Typeless: 'T' };

export function shortFormat(format: string | undefined): string {
  if (!format) return '';
  const m = /^([A-Z0-9]+?)_([A-Za-z]+)(_SRGB)?$/.exec(format);
  if (!m) return format;
  const [, channels, kind, srgb] = m;
  // R16G16B16A16 → RGBA16 (大きさがそろっていれば)。そろっていなければそのまま (R11G11B10 など)
  const parts = [...channels.matchAll(/([RGBADSX])(\d+)/g)];
  let ch = channels;
  if (parts.length > 0 && parts.every((p) => p[2] === parts[0][2]) && parts.map((p) => p[0]).join('') === channels)
    ch = parts.map((p) => p[1]).join('') + parts[0][2];
  return ch + (SUFFIX[kind] ?? `_${kind}`) + (srgb ? ' sRGB' : '');
}
