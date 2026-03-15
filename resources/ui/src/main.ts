import type {
  GraphMessage,
  InitGraphMessage,
  GraphAppendMessage,
  ToExtensionMessage,
  GraphNode,
  GraphEdge,
  QueryType,
} from './types';
import { getThemeColors, detectDarkMode } from './utils/theme';
import { GraphRenderer } from './graph/renderer';
import { bindInteractions } from './graph/interactions';
import { NodeContextMenu } from './graph/context-menu';
import { createToolbar, bindToolbarShortcuts, updateCounter } from './graph/toolbar';
import { VirtualViewport } from './graph/virtual-viewport';

// ── VSCode API ──

declare function acquireVsCodeApi(): {
  postMessage(msg: unknown): void;
  getState(): unknown;
  setState(state: unknown): void;
};

const vscode = (() => {
  try {
    return acquireVsCodeApi();
  } catch {
    // 在 VSCode 外运行时的模拟
    return {
      postMessage: (msg: unknown) => console.log('[mock vscode]', msg),
      getState: () => null,
      setState: () => {},
    };
  }
})();

/**
 * 向 VSCode 扩展发送消息
 * @param msg 消息对象
 */
function postToExtension(msg: ToExtensionMessage): void {
  vscode.postMessage(msg);
}

// ── 状态 ──

let renderer: GraphRenderer | null = null;
let contextMenu: NodeContextMenu | null = null;
let virtualViewport: VirtualViewport | null = null;
let interactionsCleanup: (() => void) | null = null;
let toolbarShortcutsCleanup: (() => void) | null = null;
let counterEl: HTMLElement | null = null;
let currentGraphType: QueryType = 'call_graph';
let initialized = false;

// ── DOM ──

const appEl = document.getElementById('app')!;
const containerEl = document.getElementById('graph-container')!;
const loadingEl = document.getElementById('cx-loading')!;

/** 显示加载指示器 */
function showLoading(): void {
  loadingEl.classList.remove('hidden');
}

/** 隐藏加载指示器 */
function hideLoading(): void {
  loadingEl.classList.add('hidden');
}

/** 显示空状态消息 */
function showEmpty(message: string = '无数据'): void {
  containerEl.innerHTML = `<div style="display:flex;align-items:center;justify-content:center;height:100%;color:#888;font-size:14px;">${message}</div>`;
}

// ── 主题 ──

const isDark = detectDarkMode();
const colors = getThemeColors(isDark);

// ── 规范化 ID 为字符串 ──

/**
 * 规范化节点数组，确保所有 ID 为字符串类型
 * @param nodes 原始节点数组
 */
function normalizeNodes(nodes: GraphNode[]): GraphNode[] {
  return nodes.map((n) => ({
    ...n,
    id: String(n.id ?? n.usr ?? ''),
  }));
}

/**
 * 规范化边数组，确保所有 caller/callee/source/target 为字符串类型
 * @param edges 原始边数组
 */
function normalizeEdges(edges: GraphEdge[]): GraphEdge[] {
  return edges.map((e) => ({
    ...e,
    caller: String(e.caller ?? e.source ?? ''),
    callee: String(e.callee ?? e.target ?? ''),
    source: String(e.caller ?? e.source ?? ''),
    target: String(e.callee ?? e.target ?? ''),
  }));
}

// ── 查找根节点 ID ──

/**
 * 在节点列表中查找根节点。
 * 优先精确匹配 querySymbol，其次部分匹配，最后使用第一个节点。
 * @param nodes 节点数组
 * @param querySymbol 查询入口符号名
 */
function findRootId(nodes: GraphNode[], querySymbol?: string): string {
  if (querySymbol) {
    const match = nodes.find((n) => n.name === querySymbol);
    if (match) return match.id;
    // 部分匹配
    const partial = nodes.find((n) => n.name.includes(querySymbol));
    if (partial) return partial.id;
  }
  return nodes.length > 0 ? nodes[0].id : '';
}

// ── 初始化图 ──

/**
 * 处理 initGraph 消息，创建并渲染新的图。
 * @param msg initGraph 消息
 */
async function handleInitGraph(msg: InitGraphMessage): Promise<void> {
  const nodes = normalizeNodes(msg.nodes ?? []);
  const edges = normalizeEdges(msg.edges ?? []);

  if (nodes.length === 0) {
    showEmpty('查询结果为空');
    hideLoading();
    return;
  }

  showLoading();
  currentGraphType = msg.graphType ?? 'call_graph';
  const rootId = findRootId(nodes, msg.querySymbol);

  // 清理之前的状态
  interactionsCleanup?.();
  toolbarShortcutsCleanup?.();
  contextMenu?.destroy();
  virtualViewport?.detach();
  renderer?.destroy();

  // 清空容器
  containerEl.innerHTML = '';

  // 创建新的渲染器
  renderer = new GraphRenderer({
    container: containerEl,
    onGotoSymbol: (file, line, column) => {
      postToExtension({ action: 'gotoSymbol', file, line, column });
    },
    onPerfReport: (report) => {
      postToExtension({ action: 'perfReport', report: report as any });
    },
  });

  try {
    await renderer.init(nodes, edges, rootId, currentGraphType, msg.queryDepth);

    // 绑定交互
    interactionsCleanup = bindInteractions(renderer, {
      onGotoSymbol: (file, line, column) => {
        postToExtension({ action: 'gotoSymbol', file, line, column });
      },
    });

    // 右键菜单
    contextMenu = new NodeContextMenu(document.body, renderer, {
      onQueryPredecessors: (symbol, file) => {
        postToExtension({
          action: 'queryPredecessors',
          graphType: currentGraphType,
          symbol,
          file,
          queryDepth: renderer?.getQueryDepth(),
        });
      },
      onQuerySuccessors: (symbol, file) => {
        postToExtension({
          action: 'querySuccessors',
          graphType: currentGraphType,
          symbol,
          file,
          queryDepth: renderer?.getQueryDepth(),
        });
      },
      onDeleteNode: async (id) => {
        await renderer?.removeNodes([id]);
        if (counterEl && renderer) updateCounter(counterEl, renderer);
      },
      onCollapseSubtree: async (id) => {
        await renderer?.collapseNode(id);
      },
      onExpandSubtree: async (id) => {
        await renderer?.expandNode(id);
      },
      onSelectSubtree: (id) => selectSubtree(id),
    }, colors);
    contextMenu.bind();

    // 工具栏
    if (!initialized) {
      const bar = createToolbar(appEl, renderer, {
        onFitView: () => renderer?.fitView(),
        onRelayout: () => renderer?.relayout(),
        onExport: () => exportJSON(),
        onDepthChange: (depth) => renderer?.setQueryDepth(depth),
        onUndo: () => renderer?.undo(),
        onRedo: () => renderer?.redo(),
      }, colors);

      counterEl = bar.querySelector('.cx-counter');

      toolbarShortcutsCleanup = bindToolbarShortcuts(renderer, {
        onFitView: () => renderer?.fitView(),
        onRelayout: () => renderer?.relayout(),
        onExport: () => exportJSON(),
        onDepthChange: () => {},
        onUndo: () => renderer?.undo(),
        onRedo: () => renderer?.redo(),
      });
      initialized = true;
    }

    // 大图视口裁剪
    virtualViewport = new VirtualViewport();
    virtualViewport.attach(renderer.getGraph()!);

    // 更新计数器
    if (counterEl) updateCounter(counterEl, renderer);

  } catch (err) {
    console.error('[main] init error:', err);
    showEmpty('渲染失败');
  } finally {
    hideLoading();
  }
}

// ── 追加数据 ──

/**
 * 处理 graphAppend 消息，增量添加节点和边。
 * @param msg graphAppend 消息
 */
async function handleGraphAppend(msg: GraphAppendMessage): Promise<void> {
  if (!renderer) return;

  const nodes = normalizeNodes(msg.nodes ?? []);
  const edges = normalizeEdges(msg.edges ?? []);

  if (nodes.length === 0 && edges.length === 0) return;

  showLoading();
  try {
    await renderer.append(nodes, edges);
    if (counterEl) updateCounter(counterEl, renderer);
  } catch (err) {
    console.error('[main] append error:', err);
  } finally {
    hideLoading();
  }
}

// ── 子树操作 ──

/**
 * 选择子树：通过 BFS 找到所有后代并设置为选中状态。
 * setElementState 是异步方法，需要 await。
 * @param nodeId 起始节点 ID
 */
async function selectSubtree(nodeId: string): Promise<void> {
  if (!renderer) return;
  const graph = renderer.getGraph();
  if (!graph) return;

  const edges = renderer.getAllEdges();
  const descendants = new Set<string>([nodeId]);
  const queue = [nodeId];
  while (queue.length > 0) {
    const current = queue.shift()!;
    for (const e of edges) {
      const src = e.caller || e.source || '';
      const tgt = e.callee || e.target || '';
      if (src === current && !descendants.has(tgt)) {
        descendants.add(tgt);
        queue.push(tgt);
      }
    }
  }

  // 选中所有后代
  const states: Record<string, string[]> = {};
  for (const id of descendants) {
    states[id] = ['selected'];
  }
  try {
    await graph.setElementState(states);
  } catch { /* ignore */ }
}

// ── 导出 ──

/** 将当前图数据导出为 JSON 文件 */
function exportJSON(): void {
  if (!renderer) return;
  const state = renderer.getState();
  const json = JSON.stringify(state, null, 2);
  const blob = new Blob([json], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `codexray-${currentGraphType}-${Date.now()}.json`;
  a.click();
  URL.revokeObjectURL(url);
}

// ── 消息监听 ──

window.addEventListener('message', async (event: MessageEvent) => {
  const msg = event.data as GraphMessage;
  if (!msg || !msg.action) return;

  switch (msg.action) {
    case 'initGraph':
      await handleInitGraph(msg);
      break;
    case 'graphAppend':
      await handleGraphAppend(msg);
      break;
  }
});

// ── 禁止浏览器默认右键菜单 ──
// G6 的 contextmenu 事件处理此操作；浏览器默认菜单会干扰右键平移。
containerEl.addEventListener('contextmenu', (e) => e.preventDefault());

// ── 通知扩展 WebView 已准备好 ──

postToExtension({ action: 'graphReady' });
