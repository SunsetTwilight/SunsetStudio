// ============================================================
//  src/editor/monacoEntry.ts を作る (Monaco を更新したら node tools/gen-monaco-entry.mjs で作り直す)
//
//  monaco-editor の editor.main.js から、エディターの機能 (contrib、検索、Go to line など) だけを取り出し、
//  言語の定義 (80 以上)、TypeScript / CSS / HTML の言語機能とワーカー、LSP は外す。JSON の言語機能は残す。
//  1 つの index.html にまとめる UI (vite-plugin-singlefile) の大きさを抑えるため。
// ============================================================
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, posix } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const mainPath = join(root, 'node_modules/monaco-editor/esm/vs/editor/editor.main.js');
const version = JSON.parse(readFileSync(join(root, 'node_modules/monaco-editor/package.json'), 'utf8')).version;
const base = '../../node_modules/monaco-editor/esm/vs/editor/';

const keep = [];
for (const line of readFileSync(mainPath, 'utf8').split(/\r?\n/)) {
  const m = /^import '(.+)';$/.exec(line.trim());
  if (!m) continue;
  const p = m[1];
  if (p.includes('/languages/definitions/')) continue; // 言語の定義 (HLSL は自作、JSON は言語機能が持つ)
  if (p.includes('iPadShowKeyboard')) continue;
  keep.push(`import '${posix.join(base, p)}';`);
}
if (keep.length < 40) throw new Error(`editor.main.js の形が変わったようです (取り出せた import が ${keep.length} 個)`);

const out = `// ============================================================
//  Monaco Editor の入口 (自動生成: node tools/gen-monaco-entry.mjs。monaco-editor ${version})
//
//  editor.main.js からエディターの機能だけを取り出し、言語の定義と TypeScript / CSS / HTML の言語機能、LSP を外した。
//  JSON の言語機能 (パイプラインのアセット) は残す。HLSL は hlsl.ts の Monarch の定義を monaco.ts で登録する。
// ============================================================
/* eslint-disable */
${keep.join('\n')}
import '${posix.join(base, '../languages/features/json/register.js')}';
export * from '${posix.join(base, 'editor.api.js')}';
`;
writeFileSync(join(root, 'src/editor/monacoEntry.ts'), out);
console.log(`src/editor/monacoEntry.ts: ${keep.length} imports (monaco-editor ${version})`);
