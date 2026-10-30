// ============================================================
//  入力 / 出力の型とフォーマットの短い表示のテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import { shortFormat, shortType } from './portTypes';

describe('短い型', () => {
  it('テクスチャは <> の中、書いていなければ float4', () => {
    expect(shortType('Texture2D<float>')).toBe('float');
    expect(shortType('Texture2D<uint>')).toBe('uint');
    expect(shortType('Texture2D')).toBe('float4');
    expect(shortType('RWTexture2D<float4>')).toBe('float4');
    expect(shortType('float4')).toBe('float4');
    expect(shortType(undefined)).toBe('');
  });
});

describe('短いフォーマット', () => {
  it('チャンネルの大きさがそろっていればまとめる', () => {
    expect(shortFormat('R16G16B16A16_Float')).toBe('RGBA16F');
    expect(shortFormat('R8G8B8A8_Unorm')).toBe('RGBA8');
    expect(shortFormat('R8G8B8A8_Unorm_SRGB')).toBe('RGBA8 sRGB');
    expect(shortFormat('R32_Uint')).toBe('R32UI');
    expect(shortFormat('D32_Float')).toBe('D32F');
    expect(shortFormat('R11G11B10_Float')).toBe('R11G11B10F');
    expect(shortFormat('')).toBe('');
  });
});
