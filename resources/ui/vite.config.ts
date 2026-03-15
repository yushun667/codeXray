import { defineConfig } from 'vite';
import { resolve } from 'path';
import { copyFileSync, existsSync } from 'fs';

export default defineConfig({
  build: {
    outDir: 'dist',
    rollupOptions: {
      input: {
        // Vite uses the HTML filename as the output name.
        // We rename index.html → graph.html via a post-build hook below.
        graph: resolve(__dirname, 'index.html'),
      },
    },
    assetsInlineLimit: 0,
    chunkSizeWarningLimit: 2000,
  },
  base: './',
  plugins: [
    {
      // Rename output index.html → graph.html after build
      name: 'rename-to-graph-html',
      closeBundle() {
        const src = resolve(__dirname, 'dist', 'index.html');
        const dst = resolve(__dirname, 'dist', 'graph.html');
        if (existsSync(src)) {
          copyFileSync(src, dst);
        }
      },
    },
  ],
});
