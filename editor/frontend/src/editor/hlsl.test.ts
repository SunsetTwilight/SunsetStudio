// ============================================================
//  HLSL の色分けの定義と、DXC のメッセージの読み取りのテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import { HLSL_TYPES, hlslMonarch, parseShaderMessages, sameShaderFile } from './hlsl';

describe('parseShaderMessages', () => {
  it('ファイル、行、列、重さ、本文を読み、抜粋の行は飛ばす', () => {
    const text = [
      "compileShader: シェーダーのコンパイルに失敗しました (D:\\p\\Assets\\shaders\\a.ps.hlsl)",
      "D:\\p\\Assets\\shaders\\a.ps.hlsl:15:61: error: expected ';' after expression",
      '  float3 n = normalize(x)',
      '                         ^',
      'D:\\p\\Assets\\shaders\\common.hlsli:3:1: warning: unused variable',
      'D:/p/Assets/shaders/a.ps.hlsl:2:5: note: declared here',
    ].join('\n');
    const d = parseShaderMessages(text);
    expect(d).toHaveLength(3);
    expect(d[0]).toEqual({ file: 'D:/p/Assets/shaders/a.ps.hlsl', line: 15, column: 61, severity: 'error', message: "expected ';' after expression" });
    expect(d[1].severity).toBe('warning');
    expect(d[2].severity).toBe('note');
    expect(sameShaderFile(d[0].file, 'shaders/a.ps.hlsl')).toBe(true);
    expect(sameShaderFile(d[1].file, 'shaders/a.ps.hlsl')).toBe(false);
  });

  it('形の合わない行は無視する', () => {
    expect(parseShaderMessages('DXC の呼び出しに失敗しました')).toEqual([]);
    expect(parseShaderMessages('')).toEqual([]);
  });
});

describe('hlslMonarch', () => {
  it('ベクトル / 行列 / テクスチャの型を持つ', () => {
    for (const t of ['float4', 'float4x4', 'uint2', 'half3', 'Texture2D', 'RWTexture2D', 'SamplerState', 'StructuredBuffer']) expect(HLSL_TYPES).toContain(t);
    expect(hlslMonarch.keywords).toContain('cbuffer');
    expect(hlslMonarch.intrinsics).toContain('saturate');
  });
});
