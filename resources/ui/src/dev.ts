/**
 * 独立开发测试入口。
 * 在浏览器中直接运行，不依赖 VSCode Webview。
 * 使用模拟数据初始化图渲染器，便于用 DevTools 调试。
 *
 * 启动：cd resources/ui && npx vite --open dev.html
 */

import type { GraphNode, GraphEdge } from './types';
import { getThemeColors } from './utils/theme';
import { GraphRenderer } from './graph/renderer';
import { bindInteractions } from './graph/interactions';
import { NodeContextMenu } from './graph/context-menu';
import { createToolbar, updateCounter } from './graph/toolbar';

// ── 模拟数据：一棵简单的调用链树 ──

function buildMockData(): { nodes: GraphNode[]; edges: GraphEdge[]; rootId: string } {
  const nodes: GraphNode[] = [
    { id: 'main', name: 'main()', file: 'main.cpp', line: 10 },
    { id: 'init', name: 'initialize()', file: 'init.cpp', line: 20 },
    { id: 'run', name: 'run_loop()', file: 'engine.cpp', line: 30 },
    { id: 'parse', name: 'parse_args()', file: 'args.cpp', line: 5 },
    { id: 'config', name: 'load_config()', file: 'config.cpp', line: 15 },
    { id: 'render', name: 'render_frame()', file: 'render.cpp', line: 100 },
    { id: 'update', name: 'update_state()', file: 'state.cpp', line: 50 },
    { id: 'draw', name: 'draw_scene()', file: 'render.cpp', line: 120 },
    { id: 'swap', name: 'swap_buffers()', file: 'render.cpp', line: 140 },
    { id: 'input', name: 'poll_input()', file: 'input.cpp', line: 10 },
    { id: 'physics', name: 'step_physics()', file: 'physics.cpp', line: 25 },
    { id: 'audio', name: 'mix_audio()', file: 'audio.cpp', line: 60 },
    { id: 'network', name: 'send_packet()', file: 'net.cpp', line: 80 },
    { id: 'log', name: 'write_log()', file: 'log.cpp', line: 42 },
    { id: 'cleanup', name: 'cleanup()', file: 'main.cpp', line: 200 },
  ];

  const edges: GraphEdge[] = [
    { caller: 'main', callee: 'init', source: 'main', target: 'init' },
    { caller: 'main', callee: 'run', source: 'main', target: 'run' },
    { caller: 'main', callee: 'cleanup', source: 'main', target: 'cleanup' },
    { caller: 'init', callee: 'parse', source: 'init', target: 'parse' },
    { caller: 'init', callee: 'config', source: 'init', target: 'config' },
    { caller: 'run', callee: 'render', source: 'run', target: 'render' },
    { caller: 'run', callee: 'update', source: 'run', target: 'update' },
    { caller: 'run', callee: 'input', source: 'run', target: 'input' },
    { caller: 'render', callee: 'draw', source: 'render', target: 'draw' },
    { caller: 'render', callee: 'swap', source: 'render', target: 'swap' },
    { caller: 'update', callee: 'physics', source: 'update', target: 'physics' },
    { caller: 'update', callee: 'audio', source: 'update', target: 'audio' },
    { caller: 'update', callee: 'network', source: 'update', target: 'network' },
    { caller: 'config', callee: 'log', source: 'config', target: 'log' },
    { caller: 'cleanup', callee: 'log', source: 'cleanup', target: 'log' },
  ];

  return { nodes, edges, rootId: 'main' };
}

// ── 初始化 ──

async function main(): Promise<void> {
  console.log('[dev] 独立测试模式启动');

  const containerEl = document.getElementById('graph-container')!;
  const appEl = document.getElementById('app')!;
  const loadingEl = document.getElementById('cx-loading')!;
  const colors = getThemeColors(true);

  const { nodes, edges, rootId } = buildMockData();
  console.log('[dev] 模拟数据:', nodes.length, '节点,', edges.length, '边');

  loadingEl.classList.remove('hidden');

  const renderer = new GraphRenderer({
    container: containerEl,
    onGotoSymbol: (file, line, col) => {
      console.log('[dev] gotoSymbol:', file, line, col);
    },
    onPerfReport: (report) => {
      console.log('[dev] perf:', report);
    },
  });

  try {
    await renderer.init(nodes, edges, rootId, 'call_graph', 2);
    console.log('[dev] 图渲染完成');

    // 绑定交互
    bindInteractions(renderer, {
      onGotoSymbol: (file, line, col) => {
        console.log('[dev] gotoSymbol:', file, line, col);
      },
    });

    // 右键菜单
    const ctxMenu = new NodeContextMenu(document.body, renderer, {
      onQueryPredecessors: (sym, file) => console.log('[dev] queryPredecessors:', sym, file),
      onQuerySuccessors: (sym, file) => console.log('[dev] querySuccessors:', sym, file),
      onDeleteNode: async (id) => {
        await renderer.removeNodes([id]);
        if (counterEl) updateCounter(counterEl, renderer);
      },
      onCollapseSubtree: async (id) => await renderer.collapseNode(id),
      onExpandSubtree: async (id) => await renderer.expandNode(id),
      onSelectSubtree: (id) => console.log('[dev] selectSubtree:', id),
    }, colors);
    ctxMenu.bind();

    // 工具栏
    const bar = createToolbar(appEl, renderer, {
      onFitView: () => renderer.fitView(),
      onRelayout: () => renderer.relayout(),
      onExport: () => console.log('[dev] export'),
      onDepthChange: (d) => console.log('[dev] depth:', d),
      onUndo: () => renderer.undo(),
      onRedo: () => renderer.redo(),
    }, colors);
    const counterEl = bar.querySelector('.cx-counter') as HTMLElement | null;
    if (counterEl) updateCounter(counterEl, renderer);

  } catch (err) {
    console.error('[dev] 初始化失败:', err);
  } finally {
    loadingEl.classList.add('hidden');
  }
}

main();
