import type { GraphRenderer } from './renderer';
import { highlightPath, clearHighlight } from './path-highlight';
import { resolveCollisions } from './layout';
import type { LayoutNode } from '../types';
import { calcNodeWidth, buildDisplayLabel } from './layout';

/**
 * 绑定所有交互增强到图渲染器。
 * 需要在图渲染完成后调用。
 *
 * @param renderer 图渲染器实例
 * @param callbacks 回调函数集
 * @returns 清理函数，调用后取消所有事件监听
 */
export function bindInteractions(
  renderer: GraphRenderer,
  callbacks: {
    onGotoSymbol?: (file: string, line: number, column: number) => void;
  },
): () => void {
  const graph = renderer.getGraph();
  if (!graph) return () => {};

  const cleanups: (() => void)[] = [];

  // ── 双击跳转到定义 ──
  const onDblClick = (e: any) => {
    const id = extractNodeId(e);
    if (!id) return;
    const raw = renderer.getNodeRaw(id);
    if (raw?.definition) {
      callbacks.onGotoSymbol?.(raw.definition.file, raw.definition.line, raw.definition.column);
    } else if (raw?.file && raw?.line) {
      callbacks.onGotoSymbol?.(raw.file, raw.line, 1);
    }
  };
  graph.on('node:dblclick', onDblClick);
  cleanups.push(() => graph.off('node:dblclick', onDblClick));

  // ── 单击节点：badge 区域→折叠/展开，其他区域→路径高亮 ──
  const onClick = async (e: any) => {
    const id = extractNodeId(e);
    if (!id) return;

    if (renderer.hasCallees(id) && isClickOnBadge(e, graph, renderer, id)) {
      await renderer.toggleCollapse(id);
      return;
    }

    await highlightPath(renderer, id);
  };
  graph.on('node:click', onClick);
  cleanups.push(() => graph.off('node:click', onClick));

  // ── 画布点击：清除高亮 ──
  const onCanvasClick = async () => {
    await clearHighlight(renderer);
  };
  graph.on('canvas:click', onCanvasClick);
  cleanups.push(() => graph.off('canvas:click', onCanvasClick));

  // ── Delete/Backspace 键：删除选中节点 ──
  const onKeyDown = async (e: KeyboardEvent) => {
    if (e.key === 'Delete' || e.key === 'Backspace') {
      const tag = (e.target as HTMLElement)?.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
      const selected = renderer.getSelectedNodeIds();
      if (selected.length > 0) {
        const rootId = renderer.getRootId();
        const toRemove = selected.filter((sid) => sid !== rootId);
        if (toRemove.length > 0) {
          await renderer.removeNodes(toRemove);
        }
      }
    }
  };
  document.addEventListener('keydown', onKeyDown);
  cleanups.push(() => document.removeEventListener('keydown', onKeyDown));

  // ── 拖拽结束：碰撞检测 + 重新计算边端口 ──
  const onDragEnd = async () => {
    const g = renderer.getGraph();
    if (!g) return;

    const nodeData = g.getNodeData();
    const layoutNodes: LayoutNode[] = [];

    for (const nd of nodeData) {
      const nid = String(nd.id);
      const raw = renderer.getNodeRaw(nid);
      if (!raw) continue;

      const pos = g.getElementPosition(nid);
      const cx = Array.isArray(pos) ? pos[0] : 0;
      const cy = Array.isArray(pos) ? pos[1] : 0;
      const label = buildDisplayLabel(raw);
      const w = calcNodeWidth(label);
      const h = 56;
      const rank = Math.round(cx / (w + 160));
      layoutNodes.push({ id: nid, rank, x: cx - w / 2, y: cy - h / 2, width: w, height: h, raw });
    }

    resolveCollisions(layoutNodes, 15);

    const updates = layoutNodes.map((ln) => ({
      id: ln.id,
      style: { x: ln.x + ln.width / 2, y: ln.y + ln.height / 2 },
    }));
    try {
      g.updateNodeData(updates);
      renderer.recomputeEdgePorts();
      await g.draw();
    } catch { /* ignore */ }
  };
  graph.on('node:dragend', onDragEnd);
  cleanups.push(() => graph.off('node:dragend', onDragEnd));

  return () => {
    for (const fn of cleanups) fn();
  };
}

/**
 * 从 G6 事件对象中提取节点 ID。
 */
function extractNodeId(e: any): string | undefined {
  const target = e.target as Record<string, unknown> | undefined;
  if (target?.id) return String(target.id);
  if (e.targetId) return String(e.targetId);
  return undefined;
}

/**
 * 判断点击是否在节点的 badge 区域（右侧折叠/展开按钮）。
 *
 * 将鼠标的浏览器坐标通过 graph.getCanvasByClient 转换到图的世界坐标系，
 * 再与节点位置（同一坐标系）直接比较。
 * 若 getCanvasByClient 不可用，降级使用 getClientByCanvas 反向比较。
 */
function isClickOnBadge(
  e: any,
  graph: any,
  renderer: GraphRenderer,
  nodeId: string,
): boolean {
  try {
    const origEvt = e.originalEvent ?? e.nativeEvent;
    if (!origEvt || typeof origEvt.clientX !== 'number') return false;
    const mouseX = origEvt.clientX;
    const mouseY = origEvt.clientY;

    // 节点在图坐标系中的中心位置
    const pos = graph.getElementPosition(nodeId);
    if (!Array.isArray(pos)) return false;
    const cx = pos[0];
    const cy = pos[1];

    // 计算节点宽度和 badge 在图坐标系中的位置（右边缘）
    const raw = renderer.getNodeRaw(nodeId);
    if (!raw) return false;
    const nodeWidth = calcNodeWidth(buildDisplayLabel(raw));
    const badgeCanvasX = cx + nodeWidth / 2;

    // 方案 A：将鼠标坐标转为图坐标系
    if (typeof graph.getCanvasByClient === 'function') {
      const cp = graph.getCanvasByClient([mouseX, mouseY]);
      const clickX = Array.isArray(cp) ? cp[0] : (cp as any)?.x;
      const clickY = Array.isArray(cp) ? cp[1] : (cp as any)?.y;
      if (typeof clickX === 'number' && typeof clickY === 'number') {
        const dx = clickX - badgeCanvasX;
        const dy = Math.abs(clickY - cy);
        return dx >= -15 && dx <= 25 && dy <= 18;
      }
    }

    // 方案 B：将 badge 坐标转为浏览器坐标系
    if (typeof graph.getClientByCanvas === 'function') {
      const clientPos = graph.getClientByCanvas([badgeCanvasX, cy]);
      const bx = Array.isArray(clientPos) ? clientPos[0] : (clientPos as any)?.x;
      const by = Array.isArray(clientPos) ? clientPos[1] : (clientPos as any)?.y;
      if (typeof bx === 'number' && typeof by === 'number') {
        return Math.abs(mouseX - bx) < 22 && Math.abs(mouseY - by) < 22;
      }
    }

    return false;
  } catch {
    return false;
  }
}
