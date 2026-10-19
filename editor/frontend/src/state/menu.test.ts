// ============================================================
//  メニューバーの関数のテスト (Vitest)
// ============================================================
import { describe, expect, it } from 'vitest';
import { menuByKey, menuLabel, nextEnabled, separator, type MenuItem } from './menu';

const items: MenuItem[] = [
  { id: 'a', label: 'A', mnemonic: 'a' },
  separator('s1'),
  { id: 'b', label: 'B', mnemonic: 'B', disabled: true },
  { id: 'c', label: 'C', mnemonic: 'c' },
];

describe('メニュー', () => {
  it('名前に下線の文字を付ける', () => {
    expect(menuLabel('ファイル', 'f')).toBe('ファイル(F)');
    expect(menuLabel('終了')).toBe('終了');
  });

  it('上下の移動は区切りと使えない項目を飛ばし、端で折り返す', () => {
    expect(nextEnabled(items, 0, 1)).toBe(3);
    expect(nextEnabled(items, 3, 1)).toBe(0);
    expect(nextEnabled(items, 0, -1)).toBe(3);
    expect(nextEnabled(items, -1, 1)).toBe(0);
    expect(nextEnabled([separator('x')], 0, 1)).toBe(-1);
    expect(nextEnabled([], 0, 1)).toBe(-1);
  });

  it('文字で項目を探す (使えない項目は選ばない)', () => {
    expect(menuByKey(items, 'C')).toBe(3);
    expect(menuByKey(items, 'b')).toBe(-1);
    expect(menuByKey(items, 'Enter')).toBe(-1);
  });
});
