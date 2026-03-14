import type { GraphEdge } from '../types';
import type { GraphRenderer } from './renderer';

/**
 * BFS 查找从 root 到 target 的有向路径。
 * 使用调用图的边方向（caller→callee 和 callee→caller 双向搜索）。
 * @param fromId 起点节点 ID
 * @param toId 终点节点 ID
 * @param edges 图的所有边
 * @returns 路径节点 ID 数组，或 null（无路径时）
 */
function bfsPath(
  fromId: string,
  toId: string,
  edges: GraphEdge[],
): string[] | null {
  if (fromId === toId) return [fromId];

  const forward = new Map<string, string[]>();
  const backward = new Map<string, string[]>();

  for (const e of edges) {
    const src = e.caller || e.source || '';
    const tgt = e.callee || e.target || '';
    if (!src || !tgt) continue;
    if (!forward.has(src)) forward.set(src, []);
    forward.get(src)!.push(tgt);
    if (!backward.has(tgt)) backward.set(tgt, []);
    backward.get(tgt)!.push(src);
  }

  const visited = new Set<string>();
  const parent = new Map<string, string>();
  const queue: string[] = [fromId];
  visited.add(fromId);

  while (queue.length > 0) {
    const current = queue.shift()!;
    if (current === toId) {
      const path: string[] = [];
      let node: string | undefined = toId;
      while (node !== undefined) {
        path.unshift(node);
        node = parent.get(node);
      }
      return path;
    }

    const neighbors = [
      ...(forward.get(current) ?? []),
      ...(backward.get(current) ?? []),
    ];
    for (const next of neighbors) {
      if (!visited.has(next)) {
        visited.add(next);
        parent.set(next, current);
        queue.push(next);
      }
    }
  }

  return null;
}

// ── 动画状态 ──

let _animFrameId: number | null = null;
let _dashOffset = 0;
let _lastDrawTime = 0;
let _isDrawing = false;
let _currentPathEdgeIds: string[] = [];
let _currentRenderer: GraphRenderer | null = null;

/** 虚线 dash-gap 模式 */
const DASH_PATTERN: [number, number] = [10, 5];
/** 每帧偏移速度（px），值越大流动越快 */
const DASH_SPEED = 0.8;
/** 两次 draw() 之间的最小间隔（ms），约 12fps */
const MIN_DRAW_INTERVAL = 80;
/** dash 循环长度（dash + gap），偏移量每到达此值归零防止溢出 */
const DASH_CYCLE = DASH_PATTERN[0] + DASH_PATTERN[1];

/**
 * 高亮从根节点到目标节点的路径。
 *
 * - 路径上的节点获得 'pathGlow' 状态（高亮边框 + 发光阴影）
 * - 路径上的边获得 'pathGlow' 状态（高亮颜色 + lineDash 虚线）
 * - 其余元素获得 'dimmed' 状态（淡化）
 * - 路径边启动流动虚线动画（requestAnimationFrame 驱动的 lineDashOffset 变化）
 *
 * @param renderer 图渲染器实例
 * @param targetId 目标节点 ID
 */
export async function highlightPath(renderer: GraphRenderer, targetId: string): Promise<void> {
  const graph = renderer.getGraph();
  if (!graph) return;

  stopAnimation();

  const rootId = renderer.getRootId();
  const edges = renderer.getAllEdges();
  const path = bfsPath(rootId, targetId, edges);

  if (!path || path.length === 0) return;

  const pathNodeSet = new Set(path);

  const pathEdgeKeys = new Set<string>();
  for (let i = 0; i < path.length - 1; i++) {
    pathEdgeKeys.add(`${path[i]}->${path[i + 1]}`);
    pathEdgeKeys.add(`${path[i + 1]}->${path[i]}`);
  }

  const allNodeIds = renderer.getAllNodeIds();
  const states: Record<string, string[]> = {};
  for (const id of allNodeIds) {
    states[id] = pathNodeSet.has(id) ? ['pathGlow'] : ['dimmed'];
  }

  const pathEdgeIds: string[] = [];
  const edgeData = graph.getEdgeData();
  for (const e of edgeData) {
    const src = String(e.source);
    const tgt = String(e.target);
    const isPath = pathEdgeKeys.has(`${src}->${tgt}`) || pathEdgeKeys.has(`${tgt}->${src}`);
    const eid = String(e.id);
    states[eid] = isPath ? ['pathGlow'] : ['dimmed'];
    if (isPath) pathEdgeIds.push(eid);
  }

  try {
    await graph.setElementState(states);
  } catch (err) {
    console.warn('[path-highlight] setElementState failed:', err);
    return;
  }

  if (pathEdgeIds.length > 0) {
    _currentPathEdgeIds = pathEdgeIds;
    _currentRenderer = renderer;
    _dashOffset = 0;
    _lastDrawTime = 0;
    _isDrawing = false;
    _animFrameId = requestAnimationFrame(flowAnimationTick);
  }
}

/**
 * requestAnimationFrame 驱动的流动虚线动画。
 * 通过周期性递减 lineDashOffset 产生虚线沿边方向流动的视觉效果。
 * 使用 graph.updateEdgeData() + graph.draw() 将偏移量传递给边映射器。
 * 以 MIN_DRAW_INTERVAL 节流，避免频繁重绘影响性能。
 */
function flowAnimationTick(timestamp: number): void {
  if (!_currentRenderer || _currentPathEdgeIds.length === 0) {
    stopAnimation();
    return;
  }

  if (timestamp - _lastDrawTime < MIN_DRAW_INTERVAL || _isDrawing) {
    _animFrameId = requestAnimationFrame(flowAnimationTick);
    return;
  }

  _lastDrawTime = timestamp;
  _dashOffset -= DASH_SPEED * (MIN_DRAW_INTERVAL / 16);

  // 防止 offset 无限增长
  if (_dashOffset < -DASH_CYCLE * 100) {
    _dashOffset = 0;
  }

  const graph = _currentRenderer.getGraph();
  if (!graph) {
    stopAnimation();
    return;
  }

  const roundedOffset = Math.round(_dashOffset);
  const updates = _currentPathEdgeIds.map((eid) => ({
    id: eid,
    data: { _dashOffset: roundedOffset },
  }));

  _isDrawing = true;
  try {
    graph.updateEdgeData(updates);
    graph
      .draw()
      .then(() => {
        _isDrawing = false;
      })
      .catch(() => {
        _isDrawing = false;
      });
  } catch {
    _isDrawing = false;
    stopAnimation();
    return;
  }

  _animFrameId = requestAnimationFrame(flowAnimationTick);
}

/** 停止流动动画，释放 rAF 和状态引用 */
function stopAnimation(): void {
  if (_animFrameId !== null) {
    cancelAnimationFrame(_animFrameId);
    _animFrameId = null;
  }
  _currentPathEdgeIds = [];
  _currentRenderer = null;
  _dashOffset = 0;
  _isDrawing = false;
  _lastDrawTime = 0;
}

/**
 * 清除所有高亮状态并停止动画。
 * 同时清除边上的 _dashOffset 动画数据。
 *
 * @param renderer 图渲染器实例
 */
export async function clearHighlight(renderer: GraphRenderer): Promise<void> {
  stopAnimation();

  const graph = renderer.getGraph();
  if (!graph) return;

  // 清除边上的动画数据
  const edgeData = graph.getEdgeData();
  const edgeUpdates = edgeData
    .filter((e: any) => (e.data as Record<string, unknown>)?._dashOffset !== undefined)
    .map((e: any) => ({ id: String(e.id), data: { _dashOffset: undefined } }));

  if (edgeUpdates.length > 0) {
    try {
      graph.updateEdgeData(edgeUpdates);
    } catch { /* ignore */ }
  }

  // 清除所有元素状态
  const nodeData = graph.getNodeData();
  const states: Record<string, string[]> = {};
  for (const n of nodeData) states[String(n.id)] = [];
  for (const e of edgeData) states[String(e.id)] = [];

  try {
    await graph.setElementState(states);
  } catch (err) {
    console.warn('[path-highlight] clearHighlight failed:', err);
  }
}
