// ============================================================
//  メニューバーの項目の形と、キーボードでの移動 (タイトルバー兼メニューバー)
//
//    MenuItem       1 つの項目。separator / submenu / run (選んだときの処理)
//    menuLabel      表示の名前 ("ファイル" + 下線を引く文字 → "ファイル(F)")
//    nextEnabled    上下の矢印で、区切りと使えない項目を飛ばして次の項目へ
//    menuByKey      Alt + 文字 / メニューを開いているときの文字で、項目を探す
//  DOM に依存しないので単体テストする (menu.test.ts)。
// ============================================================

export interface MenuItem {
  id: string;
  label?: string;
  mnemonic?: string; // 下線を引く文字 (英字 1 文字。"F" → "ファイル(F)")
  shortcut?: string; // 表示だけ ("Ctrl+Z")。キーの処理は別にある
  disabled?: boolean;
  checked?: boolean;
  separator?: boolean;
  submenu?: MenuItem[];
  title?: string; // ツールチップ
  run?: () => void | Promise<void>;
}

export interface Menu {
  id: string;
  label: string;
  mnemonic: string;
  items: MenuItem[];
}

export function menuLabel(label: string, mnemonic?: string): string {
  return mnemonic ? `${label}(${mnemonic.toUpperCase()})` : label;
}

export function isSelectable(item: MenuItem | undefined): boolean {
  return !!item && !item.separator && !item.disabled;
}

// from から dir (+1 / -1) の向きに、選べる項目を探す (端で折り返す)。なければ -1
export function nextEnabled(items: MenuItem[], from: number, dir: 1 | -1): number {
  const n = items.length;
  if (n === 0) return -1;
  let i = from;
  for (let k = 0; k < n; ++k) {
    i = (((i + dir) % n) + n) % n;
    if (isSelectable(items[i])) return i;
  }
  return -1;
}

// 下線の文字が key と同じ最初の項目 (大文字と小文字は区別しない)。なければ -1
export function menuByKey<T extends { mnemonic?: string; separator?: boolean; disabled?: boolean }>(items: T[], key: string): number {
  const k = key.toLowerCase();
  if (k.length !== 1) return -1;
  return items.findIndex((it) => !!it.mnemonic && it.mnemonic.toLowerCase() === k && !it.separator && !it.disabled);
}

export const separator = (id: string): MenuItem => ({ id, separator: true });
