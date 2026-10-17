// ============================================================
//  Vite の設定
//    npm run dev   : 開発サーバー (http://localhost:5173)。
//                    SunsetEditor を STUDIO_UI_DEV_URL=http://localhost:5173 で起動するとホットリロードできる
//    npm run build : 1 つの index.html にまとめる (vite-plugin-singlefile)。
//                    SunsetEditor は webview_set_html でこれを読み込む (file:// を使わないので OS による差がない)
// ============================================================
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig({
  plugins: [react(), viteSingleFile()],
  base: './',
  server: {
    host: '127.0.0.1',
    port: 5173,
    strictPort: true,
  },
  build: {
    target: 'es2022',
    // CMake から --outDir で bin/ui を指定する。単独で実行したときは dist/
    outDir: 'dist',
    emptyOutDir: true,
  },
});
