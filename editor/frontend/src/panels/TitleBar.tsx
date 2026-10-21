// ============================================================
//  タイトルバー兼メニューバー (VS Code のような形)
//
//  C++ が OS のタイトルバーを消し (WebHost::enableCustomFrame)、ここで描く:
//    [◆] ファイル(F) 編集(E) 表示(V) ウィンドウ(W) ヘルプ(H)   プロジェクト名 - SunsetEditor [印]   ─ □ ✕
//  ・何もない所をドラッグするとウィンドウが動く (window.startDrag。OS の移動なので、画面の端へのスナップも効く)。
//    ダブルクリックで最大化 / 元に戻す。上の端 (4 px) で大きさを変える (window.startResize)
//  ・メニュー: クリックで開き、開いているあいだはほかのメニューに乗るだけで切り替わる。
//    Alt で下線と矢印の操作、Alt + 文字で開く、↑↓ で項目、←→ でメニュー (サブメニュー)、Enter で実行、Esc で閉じる
//  ・開いたメニューはビューポート (ネイティブの子ウィンドウ) に隠れないよう、data-viewport-hole で切り抜いてもらう
//  OS のタイトルバーのまま (--native-titlebar、ブラウザーで開いたとき) は、ウィンドウのボタンとドラッグを出さない。
// ============================================================
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { call, isConnected, on } from '../bridge/client';
import type { RecentProject } from '../bridge/protocol';
import {
  closeProject,
  closeWindow,
  floatViewport,
  importAsset,
  migrateMaterials,
  migrateProject,
  minimizeWindow,
  newProject,
  openDevTools,
  openProjectDialog,
  openRecent,
  ping,
  recentProjects,
  saveAll,
  toggleMaximize,
  undoRedo,
} from '../state/commands';
import { PANEL_TITLES, popoutPanel, resetLayout, showPanel } from '../state/dock';
import { isSelectable, menuByKey, nextEnabled, separator, type Menu, type MenuItem } from '../state/menu';
import { studioStore, useStudio } from '../state/store';
import { setViewer } from './ViewerPanel';

type Dialog = 'about' | 'controls' | null;

export function TitleBar() {
  const info = useStudio((s) => s.info);
  const project = useStudio((s) => s.project);
  const edit = useStudio((s) => s.edit);
  const viewer = useStudio((s) => s.viewer);
  const previews = useStudio((s) => s.previews).filter((p) => p.mode === 'window');
  const floating = useStudio((s) => s.viewportFloating);
  const maximized = useStudio((s) => s.windowMaximized);
  const commandError = useStudio((s) => s.commandError);
  const unsavedAny = useStudio((s) => s.unsaved?.any ?? false);
  const sceneDirty = useStudio((s) => s.scene?.dirty ?? false);
  const unsaved = unsavedAny || sceneDirty; // 保存していない変更 (グラフ、パイプライン、シーン)
  const storeNotice = useStudio((s) => s.notice);
  const frame = isConnected() && !!info?.window?.customFrame;
  const devTools = !!info?.window?.devTools;
  const open = !!project?.open;

  const [openMenu, setOpenMenu] = useState(-1); // 開いているメニュー
  const [active, setActive] = useState(-1); // 開いたメニューの中の選んでいる項目
  const [subActive, setSubActive] = useState(-1); // サブメニューの中の項目 (-1 = 閉じている)
  const [keyboard, setKeyboard] = useState(false); // Alt で入ったキーボードの操作 (下線を出す)
  const [barFocus, setBarFocus] = useState(-1); // キーボードの操作で、メニューを開かずに選んでいる見出し
  const [recent, setRecent] = useState<RecentProject[]>([]);
  const [dialog, setDialog] = useState<Dialog>(null);
  const [notice, setNotice] = useState('');
  const barRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!storeNotice) return;
    const t = window.setTimeout(() => studioStore.setNotice(''), 4000);
    return () => window.clearTimeout(t);
  }, [storeNotice]);

  const menus: Menu[] = useMemo(() => {
    const panel = (id: string, mnemonic?: string): MenuItem => ({
      id: `view.${id}`,
      label: PANEL_TITLES[id] ?? id,
      mnemonic,
      disabled: !open,
      run: () => showPanel(id),
    });
    return [
      {
        id: 'file',
        label: 'ファイル',
        mnemonic: 'F',
        items: [
          { id: 'new', label: '新しいプロジェクト…', mnemonic: 'N', disabled: !open, title: '起動画面でテンプレートから作る (今のプロジェクトは閉じる)', run: newProject },
          { id: 'open', label: '開く…', mnemonic: 'O', run: openProjectDialog },
          {
            id: 'recent',
            label: '最近使ったプロジェクト',
            mnemonic: 'R',
            disabled: recent.length === 0,
            submenu: recent.map((p) => ({ id: `recent.${p.file}`, label: p.name, title: p.file, disabled: !p.exists, run: () => openRecent(p) })),
          },
          separator('s1'),
          { id: 'save', label: 'すべて保存', mnemonic: 'S', shortcut: 'Ctrl+S', disabled: !open, title: '保存していないレンダーグラフ (ノードエディター)、パイプライン、シーンをファイルに書く', run: saveAll },
          separator('s2'),
          { id: 'import', label: 'アセットを取り込む…', mnemonic: 'I', disabled: !open, title: 'assets/ にコピーして取り込む (モデル / テクスチャ)', run: () => importAsset('copy') },
          { id: 'link', label: '外部のファイルを参照…', mnemonic: 'L', disabled: !open, title: 'コピーせず、外部のファイルを参照する', run: () => importAsset('link') },
          ...(open && project?.legacyLayout
            ? [separator('s3'), { id: 'migrate', label: '古い構成 (Assets/) を新しい構成に移す…', run: migrateProject } as MenuItem]
            : []),
          ...(open && project?.materialMigration?.changes.length
            ? [separator('s3m'), { id: 'migrateMaterials', label: 'マテリアルなしの描画に移す…', title: 'マテリアルを前提にした描画のファイルを書き換える', run: migrateMaterials } as MenuItem]
            : []),
          separator('s4'),
          { id: 'close', label: 'プロジェクトを閉じる', mnemonic: 'C', disabled: !open, run: closeProject },
          separator('s5'),
          { id: 'exit', label: '終了', mnemonic: 'X', shortcut: 'Alt+F4', run: closeWindow },
        ],
      },
      {
        id: 'edit',
        label: '編集',
        mnemonic: 'E',
        items: [
          { id: 'undo', label: edit?.canUndo ? `元に戻す: ${edit.undoLabel}` : '元に戻す', mnemonic: 'U', shortcut: 'Ctrl+Z', disabled: !edit?.canUndo, run: () => undoRedo(true) },
          { id: 'redo', label: edit?.canRedo ? `やり直す: ${edit.redoLabel}` : 'やり直す', mnemonic: 'R', shortcut: 'Ctrl+Y', disabled: !edit?.canRedo, run: () => undoRedo(false) },
        ],
      },
      {
        id: 'view',
        label: '表示',
        mnemonic: 'V',
        items: [
          panel('scene', 'S'),
          panel('library', 'L'),
          panel('graph', 'G'),
          panel('inspector', 'I'),
          panel('nodes', 'N'),
          panel('viewer', 'V'),
          panel('log', 'O'),
          separator('s1'),
          {
            id: 'viewer.off',
            label: '最終出力に戻す',
            disabled: !(open && viewer?.enabled),
            title: 'リソースビューアーの表示をやめ、ビューポートを最終出力に戻す',
            run: () => void setViewer({ enabled: false }),
          },
          { id: 'reset', label: 'パネルの配置を元に戻す', mnemonic: 'R', disabled: !open, run: resetLayout },
        ],
      },
      {
        id: 'window',
        label: 'ウィンドウ',
        mnemonic: 'W',
        items: [
          floating
            ? { id: 'unfloat', label: 'ビューポートをメインのウィンドウに戻す', mnemonic: 'V', disabled: !open, run: () => floatViewport(false) }
            : { id: 'float', label: 'ビューポートを別ウィンドウに', mnemonic: 'V', disabled: !open, title: 'タブをウィンドウの外へドラッグしても同じ', run: () => void popoutPanel('viewport') },
          ...(frame
            ? [
                separator('s1'),
                { id: 'min', label: '最小化', mnemonic: 'N', run: minimizeWindow } as MenuItem,
                { id: 'max', label: maximized ? '元のサイズに戻す' : '最大化', mnemonic: 'X', run: toggleMaximize } as MenuItem,
              ]
            : []),
        ],
      },
      {
        id: 'help',
        label: 'ヘルプ',
        mnemonic: 'H',
        items: [
          { id: 'controls', label: 'ビューポートの操作…', mnemonic: 'C', run: () => setDialog('controls') },
          separator('s1'),
          { id: 'devtools', label: '開発者ツール', mnemonic: 'D', shortcut: 'F12', disabled: !devTools, title: devTools ? '' : '--devtools か STUDIO_DEVTOOLS=1 で起動すると使えます', run: openDevTools },
          {
            id: 'ping',
            label: 'C++ との通信を確かめる (Ping)',
            mnemonic: 'P',
            disabled: !isConnected(),
            run: async () => {
              setNotice(`Ping: ${await ping()}`);
              window.setTimeout(() => setNotice(''), 4000);
            },
          },
          separator('s2'),
          { id: 'about', label: 'バージョン情報…', mnemonic: 'A', run: () => setDialog('about') },
        ],
      },
    ];
  }, [open, project?.legacyLayout, project?.materialMigration, recent, edit, viewer?.enabled, floating, frame, maximized, devTools]);

  const close = useCallback(() => {
    setOpenMenu(-1);
    setActive(-1);
    setSubActive(-1);
    setKeyboard(false);
    setBarFocus(-1);
  }, []);

  const openAt = useCallback(
    (index: number, fromKeyboard: boolean) => {
      setOpenMenu(index);
      setSubActive(-1);
      setActive(fromKeyboard ? nextEnabled(menus[index].items, -1, 1) : -1);
      setBarFocus(index);
      if (menus[index].id === 'file') void recentProjects().then(setRecent);
    },
    [menus],
  );

  const activate = useCallback(
    (item: MenuItem | undefined) => {
      if (!item || !isSelectable(item) || item.submenu) return;
      close();
      void item.run?.();
    },
    [close],
  );

  // 起動したら最近のプロジェクトを読んでおく (ファイルのメニューを開いたときにも読み直す)
  useEffect(() => {
    void recentProjects().then(setRecent);
  }, [project?.file]);

  // 最大化の状態 (ボタンの形)。ダブルクリックや Win + ↑ でも変わる
  useEffect(() => on('window.state', (e) => studioStore.setWindowMaximized(e.maximized)), []);

  // メニューの外を押したら閉じる
  useEffect(() => {
    if (openMenu < 0 && !keyboard) return;
    const onDown = (e: MouseEvent) => {
      const t = e.target as HTMLElement | null;
      if (t?.closest('.menubar') || t?.closest('.menu-dropdown')) return;
      close();
    };
    window.addEventListener('mousedown', onDown, true);
    window.addEventListener('blur', close);
    return () => {
      window.removeEventListener('mousedown', onDown, true);
      window.removeEventListener('blur', close);
    };
  }, [openMenu, keyboard, close]);

  // キーボード: Alt / Alt + 文字 / 矢印 / Enter / Esc
  useEffect(() => {
    let altAlone = false;
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Alt') {
        altAlone = !e.repeat;
        if (openMenu >= 0 || keyboard) e.preventDefault();
        return;
      }
      altAlone = false;
      // Alt + 文字でメニューを開く
      if (e.altKey && !e.ctrlKey && !e.metaKey) {
        const i = menuByKey(menus, e.key);
        if (i >= 0) {
          e.preventDefault();
          setKeyboard(true);
          openAt(i, true);
        }
        return;
      }
      if (openMenu < 0 && !keyboard) return;
      const menu = openMenu >= 0 ? menus[openMenu] : null;
      const items = menu?.items ?? [];
      const sub = active >= 0 ? items[active]?.submenu : undefined;
      const inSub = !!sub && subActive >= 0;
      const switchMenu = (dir: 1 | -1) => {
        const i = ((Math.max(openMenu, barFocus, 0) + dir) % menus.length + menus.length) % menus.length;
        if (openMenu >= 0) openAt(i, true);
        else setBarFocus(i);
      };
      let handled = true;
      switch (e.key) {
        case 'Escape':
          if (inSub) setSubActive(-1);
          else if (openMenu >= 0 && keyboard) {
            setOpenMenu(-1);
            setActive(-1);
          } else close();
          break;
        case 'ArrowDown':
          if (openMenu < 0) openAt(Math.max(barFocus, 0), true);
          else if (inSub) setSubActive(nextEnabled(sub!, subActive, 1));
          else setActive(nextEnabled(items, active, 1));
          break;
        case 'ArrowUp':
          if (openMenu < 0) openAt(Math.max(barFocus, 0), true);
          else if (inSub) setSubActive(nextEnabled(sub!, subActive, -1));
          else setActive(nextEnabled(items, active, -1));
          break;
        case 'ArrowRight':
          if (sub && !inSub && isSelectable(items[active])) setSubActive(nextEnabled(sub, -1, 1));
          else switchMenu(1);
          break;
        case 'ArrowLeft':
          if (inSub) setSubActive(-1);
          else switchMenu(-1);
          break;
        case 'Enter':
        case ' ':
          if (openMenu < 0) openAt(Math.max(barFocus, 0), true);
          else if (inSub) activate(sub![subActive]);
          else if (sub && isSelectable(items[active])) setSubActive(nextEnabled(sub, -1, 1));
          else activate(items[active]);
          break;
        default: {
          // メニューを開いているときは、文字で項目を選ぶ。見出しにいるときは、文字でメニューを開く
          if (e.ctrlKey || e.metaKey) {
            handled = false;
            break;
          }
          if (openMenu >= 0) {
            const list = inSub ? sub! : items;
            const i = menuByKey(list, e.key);
            if (i >= 0) {
              if (inSub) activate(list[i]);
              else if (list[i].submenu) {
                setActive(i);
                setSubActive(nextEnabled(list[i].submenu!, -1, 1));
              } else activate(list[i]);
            }
          } else {
            const i = menuByKey(menus, e.key);
            if (i >= 0) openAt(i, true);
          }
        }
      }
      if (handled) {
        e.preventDefault();
        e.stopPropagation();
      }
    };
    const onKeyUp = (e: KeyboardEvent) => {
      if (e.key !== 'Alt' || !altAlone) return;
      altAlone = false;
      e.preventDefault();
      // Alt だけを押して離したら、メニューバーのキーボードの操作に入る (もう一度で出る)
      if (keyboard || openMenu >= 0) close();
      else {
        setKeyboard(true);
        setBarFocus(0);
      }
    };
    window.addEventListener('keydown', onKeyDown, true);
    window.addEventListener('keyup', onKeyUp, true);
    return () => {
      window.removeEventListener('keydown', onKeyDown, true);
      window.removeEventListener('keyup', onKeyUp, true);
    };
  }, [menus, openMenu, active, subActive, keyboard, barFocus, openAt, activate, close]);

  // 何もない所のドラッグでウィンドウを動かす。ダブルクリックで最大化 / 元に戻す
  const onBarMouseDown = (e: React.MouseEvent) => {
    if (!frame || e.button !== 0) return;
    const t = e.target as HTMLElement;
    if (t.closest('button, .menu-dropdown, .chip, input, select, a')) return;
    e.preventDefault();
    if (e.detail >= 2) void toggleMaximize();
    else void call('window.startDrag').catch(() => {});
  };
  const resize = (edge: 'top' | 'topleft' | 'topright') => (e: React.MouseEvent) => {
    if (e.button !== 0) return;
    e.preventDefault();
    e.stopPropagation();
    void call('window.startResize', { edge }).catch(() => {});
  };

  const title = open ? `${project?.name}${unsaved ? ' ●' : ''} - SunsetEditor` : 'SunsetEditor';
  useEffect(() => {
    document.title = title;
  }, [title]);

  return (
    <header className={`titlebar${frame ? ' custom-frame' : ''}${maximized ? ' maximized' : ''}`} ref={barRef} onMouseDown={onBarMouseDown}>
      {frame && !maximized && (
        <>
          <div className="titlebar-resize top" onMouseDown={resize('top')} />
          <div className="titlebar-resize topleft" onMouseDown={resize('topleft')} />
          <div className="titlebar-resize topright" onMouseDown={resize('topright')} />
        </>
      )}
      <span className="titlebar-icon" aria-hidden>
        ◆
      </span>
      <nav className="menubar" role="menubar">
        {menus.map((m, i) => (
          <div key={m.id} className="menubar-entry">
            <button
              role="menuitem"
              aria-haspopup="menu"
              aria-expanded={openMenu === i}
              className={`menubar-button${openMenu === i ? ' open' : ''}${keyboard && barFocus === i && openMenu < 0 ? ' focus' : ''}`}
              onMouseDown={(e) => {
                e.preventDefault();
                if (openMenu === i) close();
                else openAt(i, false);
              }}
              onMouseEnter={() => {
                if (openMenu >= 0 && openMenu !== i) openAt(i, false);
              }}
            >
              <MnemonicLabel label={m.label} mnemonic={m.mnemonic} show={keyboard || openMenu >= 0} />
            </button>
            {openMenu === i && (
              <MenuDropdown
                items={m.items}
                active={active}
                subActive={subActive}
                showMnemonic={keyboard}
                onHover={(j) => {
                  setActive(j);
                  setSubActive(m.items[j]?.submenu && isSelectable(m.items[j]) ? -2 : -1);
                }}
                onSubHover={setSubActive}
                onActivate={activate}
              />
            )}
          </div>
        ))}
      </nav>
      <div className="titlebar-center">
        <span className="titlebar-title" title={project?.file}>
          {title}
        </span>
        {open && project?.legacyLayout && (
          <span className="chip legacy" title="アセットが Assets/ の下にある古い構成です。ファイル > 古い構成を新しい構成に移す">
            古い構成 (Assets/)
            <button onClick={() => void migrateProject()}>移す</button>
          </span>
        )}
        {open && !!project?.materialMigration?.changes.length && (
          <span className="chip legacy" title={'マテリアルを前提にした描画のファイルがあります (このままでは正しく描けません):\n' + project.materialMigration.changes.join('\n')}>
            マテリアルのころのプロジェクト
            <button onClick={() => void migrateMaterials()}>移す</button>
          </span>
        )}
        {open && viewer?.enabled && viewer.resource && (
          <span className="chip viewing" title="ビューポートに、最終出力の代わりにこのリソースを表示しています">
            表示中: {viewer.resource}
            <button onClick={() => void setViewer({ enabled: false })} title="ビューポートを最終出力に戻す">
              ✕
            </button>
          </span>
        )}
        {open && previews.length > 0 && (
          <span className="chip" title={previews.map((p) => p.resource + (p.status ? ` (${p.status})` : '')).join('\n')}>
            別ウィンドウ {previews.length}
          </span>
        )}
        {commandError && (
          <span className="chip error" title={commandError}>
            {commandError.split('\n')[0]}
            <button onClick={() => studioStore.setCommandError('')} title="消す">
              ✕
            </button>
          </span>
        )}
        {(notice || storeNotice) && <span className="muted titlebar-notice">{notice || storeNotice}</span>}
      </div>
      {frame && (
        <div className="window-controls">
          <button className="window-button" onClick={() => void minimizeWindow()} title="最小化" aria-label="最小化">
            {''}
          </button>
          <button className="window-button" onClick={() => void toggleMaximize()} title={maximized ? '元のサイズに戻す' : '最大化'} aria-label="最大化">
            {maximized ? '' : ''}
          </button>
          <button className="window-button close" onClick={() => void closeWindow()} title="閉じる" aria-label="閉じる">
            {''}
          </button>
        </div>
      )}
      {dialog && <InfoDialog kind={dialog} onClose={() => setDialog(null)} />}
    </header>
  );
}

function MnemonicLabel({ label, mnemonic, show }: { label: string; mnemonic?: string; show: boolean }) {
  if (!mnemonic) return <>{label}</>;
  return (
    <>
      {label}
      <span className="mnemonic">
        (<span className={show ? 'underline' : ''}>{mnemonic.toUpperCase()}</span>)
      </span>
    </>
  );
}

function MenuDropdown({
  items,
  active,
  subActive,
  showMnemonic,
  onHover,
  onSubHover,
  onActivate,
}: {
  items: MenuItem[];
  active: number;
  subActive: number;
  showMnemonic: boolean;
  onHover: (i: number) => void;
  onSubHover: (i: number) => void;
  onActivate: (item: MenuItem) => void;
}) {
  return (
    <div className="menu-dropdown" role="menu" data-viewport-hole>
      {items.map((it, i) =>
        it.separator ? (
          <div key={it.id} className="menu-separator" role="separator" />
        ) : (
          <div key={it.id} className="menu-item-wrap">
            <button
              role="menuitem"
              className={`menu-item${i === active ? ' active' : ''}`}
              disabled={it.disabled}
              title={it.title}
              onMouseEnter={() => onHover(i)}
              onMouseDown={(e) => e.preventDefault()}
              onClick={() => (it.submenu ? onHover(i) : onActivate(it))}
            >
              <span className="menu-check">{it.checked ? '✓' : ''}</span>
              <span className="menu-label">
                <MnemonicLabel label={it.label ?? ''} mnemonic={it.mnemonic} show={showMnemonic} />
              </span>
              <span className="menu-shortcut">{it.submenu ? '›' : it.shortcut ?? ''}</span>
            </button>
            {it.submenu && i === active && subActive !== -1 && !it.disabled && (
              <div className="menu-dropdown submenu" role="menu" data-viewport-hole>
                {it.submenu.map((s, j) => (
                  <button
                    key={s.id}
                    role="menuitem"
                    className={`menu-item${j === subActive ? ' active' : ''}`}
                    disabled={s.disabled}
                    title={s.title}
                    onMouseEnter={() => onSubHover(j)}
                    onMouseDown={(e) => e.preventDefault()}
                    onClick={() => onActivate(s)}
                  >
                    <span className="menu-check" />
                    <span className="menu-label">{s.label}</span>
                    <span className="menu-shortcut" />
                  </button>
                ))}
              </div>
            )}
          </div>
        ),
      )}
    </div>
  );
}

function InfoDialog({ kind, onClose }: { kind: 'about' | 'controls'; onClose: () => void }) {
  const info = useStudio((s) => s.info);
  const engine = useStudio((s) => s.engine);
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);
  return (
    <div className="modal-backdrop" onMouseDown={onClose}>
      <div className="modal info-dialog" data-viewport-hole onMouseDown={(e) => e.stopPropagation()}>
        {kind === 'about' ? (
          <>
            <div className="modal-title">SunsetEditor</div>
            <p>
              {info?.app} {info?.version} · protocol {info?.protocol}
            </p>
            <p className="muted">
              エンジン: {engine?.adapter ?? '—'} / {engine?.api ?? '—'} · ABI {engine?.abi ?? info?.engine.abi}
            </p>
            <p className="muted">SunsetGraphicsEngine + SunsetMathLib + SunsetAsset。AI (Claude / Claude Code) を使って作成しています。</p>
          </>
        ) : (
          <>
            <div className="modal-title">ビューポートの操作</div>
            <table className="controls-table">
              <tbody>
                <tr><td>左ドラッグ</td><td>回転</td></tr>
                <tr><td>中 / 右ドラッグ</td><td>平行移動</td></tr>
                <tr><td>ホイール</td><td>ズーム</td></tr>
                <tr><td>F</td><td>視点を戻す</td></tr>
                <tr><td>左クリック</td><td>物体を選ぶ (インスペクターで編集)</td></tr>
                <tr><td>Ctrl+Z / Ctrl+Y</td><td>元に戻す / やり直す</td></tr>
              </tbody>
            </table>
          </>
        )}
        <div className="modal-actions">
          <button onClick={onClose}>閉じる</button>
        </div>
      </div>
    </div>
  );
}
