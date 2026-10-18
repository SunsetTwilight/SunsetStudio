// ============================================================
//  Monaco Editor の初期化 (M4 のシェーダー編集)
//
//  UI は 1 つの index.html にまとめる (vite-plugin-singlefile) ので、Monaco もそこに入る。
//  ワーカーは ?worker&inline で Blob として埋め込む。作れない環境では Monaco が本体のスレッドで代わりに動く。
//  言語: HLSL (Monarch の自作の定義、hlsl.ts) と JSON (パイプラインのアセット)。
// ============================================================
import * as monaco from './monacoEntry';
import EditorWorker from 'monaco-editor/editor/editor.worker.js?worker&inline';
import JsonWorker from 'monaco-editor/language/json/json.worker.js?worker&inline';
import { hlslLanguageConfiguration, hlslMonarch } from './hlsl';

declare global {
  interface Window {
    MonacoEnvironment?: { getWorker(workerId: string, label: string): Worker };
  }
}

let ready = false;

export function setupMonaco(): typeof monaco {
  if (ready) return monaco;
  ready = true;
  window.MonacoEnvironment = {
    getWorker(_id: string, label: string) {
      return label === 'json' ? new JsonWorker() : new EditorWorker();
    },
  };
  monaco.languages.register({ id: 'hlsl', extensions: ['.hlsl', '.hlsli', '.fx'], aliases: ['HLSL', 'hlsl'] });
  monaco.languages.setMonarchTokensProvider('hlsl', hlslMonarch as unknown as monaco.languages.IMonarchLanguage);
  monaco.languages.setLanguageConfiguration('hlsl', hlslLanguageConfiguration);
  // エディターの暗い配色に合わせる (セマンティクスとレジスターは目立たせる)
  monaco.editor.defineTheme('sunset-dark', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: 'type.semantic', foreground: 'c586c0' },
      { token: 'number.register', foreground: 'd7ba7d' },
      { token: 'predefined', foreground: 'dcdcaa' },
      { token: 'annotation', foreground: '9cdcfe' },
    ],
    colors: { 'editor.background': '#1e1f22' },
  });
  return monaco;
}

export function languageOf(path: string): string {
  if (/\.(hlsl|hlsli|fx)$/i.test(path)) return 'hlsl';
  if (/\.json$/i.test(path)) return 'json';
  return 'plaintext';
}

export { monaco };
