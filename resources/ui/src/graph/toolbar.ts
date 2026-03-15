import type { ThemeColors } from '../types';
import type { GraphRenderer } from './renderer';
import { clearHighlight } from './path-highlight';
import { buildDisplayLabel } from './layout';

export interface ToolbarCallbacks {
  onFitView: () => void;
  onRelayout: () => void;
  onExport: () => void;
  onDepthChange: (depth: number) => void;
  onUndo: () => void;
  onRedo: () => void;
}

/**
 * Create the toolbar DOM element and attach to the container.
 */
export function createToolbar(
  appContainer: HTMLElement,
  renderer: GraphRenderer,
  callbacks: ToolbarCallbacks,
  colors: ThemeColors,
): HTMLElement {
  const bar = document.createElement('div');
  bar.className = 'cx-toolbar';
  bar.style.cssText = `
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 6px 12px;
    background: ${colors.toolbarBg};
    border-bottom: 1px solid ${colors.toolbarBorder};
    font-size: 12px;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    color: ${colors.fg};
    flex-shrink: 0;
    z-index: 10;
    flex-wrap: wrap;
  `;

  // ── Buttons ──
  const btnStyle = `
    padding: 4px 10px;
    border: 1px solid ${colors.toolbarBorder};
    border-radius: 4px;
    background: transparent;
    color: ${colors.fg};
    cursor: pointer;
    font-size: 12px;
    white-space: nowrap;
  `;

  const btnFit = createButton('适应画布 (F)', btnStyle, callbacks.onFitView);
  const btnRelayout = createButton('重新布局 (R)', btnStyle, callbacks.onRelayout);
  const btnZoomIn = createButton('+', btnStyle, () => {
    const graph = renderer.getGraph();
    if (graph) graph.zoomTo(1.2, false);
  });
  const btnZoomOut = createButton('−', btnStyle, () => {
    const graph = renderer.getGraph();
    if (graph) graph.zoomTo(0.8, false);
  });
  const btnUndo = createButton('↩ Undo', btnStyle, callbacks.onUndo);
  const btnRedo = createButton('↪ Redo', btnStyle, callbacks.onRedo);
  const btnExport = createButton('导出 JSON', btnStyle, callbacks.onExport);

  // ── Search ──
  const searchInput = document.createElement('input');
  searchInput.type = 'text';
  searchInput.placeholder = '搜索节点…';
  searchInput.style.cssText = `
    padding: 4px 8px;
    border: 1px solid ${colors.searchBorder};
    border-radius: 4px;
    background: ${colors.searchBg};
    color: ${colors.fg};
    font-size: 12px;
    width: 140px;
    outline: none;
  `;
  searchInput.addEventListener('input', () => {
    applySearch(renderer, searchInput.value.trim());
  });

  // ── Depth selector ──
  const depthLabel = document.createElement('span');
  depthLabel.textContent = '深度:';
  depthLabel.style.marginLeft = '8px';

  const depthSelect = document.createElement('select');
  depthSelect.style.cssText = `
    padding: 3px 6px;
    border: 1px solid ${colors.searchBorder};
    border-radius: 4px;
    background: ${colors.searchBg};
    color: ${colors.fg};
    font-size: 12px;
    outline: none;
  `;
  for (let d = 1; d <= 5; d++) {
    const opt = document.createElement('option');
    opt.value = String(d);
    opt.textContent = String(d);
    if (d === 3) opt.selected = true;
    depthSelect.appendChild(opt);
  }
  depthSelect.addEventListener('change', () => {
    callbacks.onDepthChange(parseInt(depthSelect.value, 10));
  });

  // ── Counter ──
  const counter = document.createElement('span');
  counter.className = 'cx-counter';
  counter.style.cssText = `
    margin-left: auto;
    color: ${colors.fg};
    opacity: 0.6;
    font-size: 11px;
    white-space: nowrap;
  `;
  updateCounter(counter, renderer);

  // ── Hint ──
  const hint = document.createElement('span');
  hint.textContent = '双击跳转 · 右键菜单 · 框选后Delete删除';
  hint.style.cssText = `
    color: ${colors.fg};
    opacity: 0.4;
    font-size: 11px;
    white-space: nowrap;
  `;

  bar.append(
    btnFit, btnRelayout, btnZoomIn, btnZoomOut,
    btnUndo, btnRedo,
    searchInput,
    depthLabel, depthSelect,
    btnExport,
    counter,
    hint,
  );

  // Insert at top of app container
  appContainer.insertBefore(bar, appContainer.firstChild);

  return bar;
}

/**
 * Bind global keyboard shortcuts for the toolbar.
 */
export function bindToolbarShortcuts(
  renderer: GraphRenderer,
  callbacks: ToolbarCallbacks,
): () => void {
  const handler = (e: KeyboardEvent) => {
    // Ignore if focus is in an input
    const tag = (e.target as HTMLElement)?.tagName;
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;

    if (e.key === 'f' || e.key === 'F') {
      e.preventDefault();
      callbacks.onFitView();
    } else if (e.key === 'r' || e.key === 'R') {
      e.preventDefault();
      callbacks.onRelayout();
    } else if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key === 'z') {
      e.preventDefault();
      callbacks.onRedo();
    } else if ((e.metaKey || e.ctrlKey) && e.key === 'z') {
      e.preventDefault();
      callbacks.onUndo();
    }
  };

  document.addEventListener('keydown', handler);
  return () => document.removeEventListener('keydown', handler);
}

/**
 * 搜索并高亮匹配节点，淡化其他节点。
 * setElementState 是异步方法，需要 await。
 * @param renderer 图渲染器实例
 * @param keyword 搜索关键词
 */
async function applySearch(renderer: GraphRenderer, keyword: string): Promise<void> {
  const graph = renderer.getGraph();
  if (!graph) return;

  if (!keyword) {
    await clearHighlight(renderer);
    return;
  }

  const kw = keyword.toLowerCase();
  const allNodes = renderer.getAllNodes();
  const states: Record<string, string[]> = {};

  for (const n of allNodes) {
    const label = buildDisplayLabel(n).toLowerCase();
    const match = label.includes(kw);
    states[n.id] = match ? ['highlight'] : ['dimmed'];
  }

  // Dim all edges during search
  const edgeData = graph.getEdgeData();
  for (const e of edgeData) {
    states[String(e.id)] = ['dimmed'];
  }

  try {
    await graph.setElementState(states);
  } catch { /* ignore */ }
}

/** Update the node/edge counter display */
export function updateCounter(el: HTMLElement, renderer: GraphRenderer): void {
  el.textContent = `节点: ${renderer.getNodeCount()} | 边: ${renderer.getEdgeCount()}`;
}

function createButton(label: string, style: string, onClick: () => void): HTMLElement {
  const btn = document.createElement('button');
  btn.textContent = label;
  btn.style.cssText = style;
  btn.addEventListener('click', onClick);
  return btn;
}
