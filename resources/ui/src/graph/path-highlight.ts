import type { GraphEdge } from '../types';
import type { GraphRenderer } from './renderer';

/**
 * BFS 查找从 fromId 到 toId 的最短路径。
 * 同时沿 caller→callee 和 callee→caller 方向搜索，
 * 确保无论边方向如何均能找到连通路径。
 */
function bfsPath(fromId: string, toId: string, edges: GraphEdge[]): string[] | null {
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

  const visited = new Set<string>([fromId]);
  const parent = new Map<string, string>();
  const queue = [fromId];

  while (queue.length > 0) {
    const cur = queue.shift()!;
    if (cur === toId) {
      const path: string[] = [];
      let n: string | undefined = toId;
      while (n !== undefined) {
        path.unshift(n);
        n = parent.get(n);
      }
      return path;
    }
    for (const next of [...(forward.get(cur) ?? []), ...(backward.get(cur) ?? [])]) {
      if (!visited.has(next)) {
        visited.add(next);
        parent.set(next, cur);
        queue.push(next);
      }
    }
  }
  return null;
}

// ── 脉冲动画状态 ──

let _pulseTimer: ReturnType<typeof setInterval> | null = null;
let _pulsePhase = 0;
let _pathEdgeIds: string[] = [];
let _activeRenderer: GraphRenderer | null = null;

const PULSE_INTERVAL = 600;

/**
 * 高亮从根节点到目标节点的路径。
 *
 * 工作流程：
 * 1. 禁用 hover-activate（防止 pointer-leave 清除所有状态）
 * 2. BFS 查找根→目标路径
 * 3. 路径节点设 pathGlow，路径边设 pathGlow，其余设 dimmed
 * 4. 启动脉冲动画：路径边在 pathGlow/pathGlowDim 之间交替切换
 */
export async function highlightPath(renderer: GraphRenderer, targetId: string): Promise<void> {
  const graph = renderer.getGraph();
  if (!graph) return;

  // 先清除已有高亮
  await clearHighlight(renderer);

  const rootId = renderer.getRootId();
  const edges = renderer.getAllEdges();
  const path = bfsPath(rootId, targetId, edges);
  if (!path || path.length === 0) return;

  // 构建路径节点集合
  const pathNodeSet = new Set(path);

  // 构建路径边的 key 集合（双向匹配）
  const pathEdgeKeys = new Set<string>();
  for (let i = 0; i < path.length - 1; i++) {
    pathEdgeKeys.add(`${path[i]}->${path[i + 1]}`);
    pathEdgeKeys.add(`${path[i + 1]}->${path[i]}`);
  }

  // 为所有节点分配状态
  const states: Record<string, string[]> = {};
  for (const id of renderer.getAllNodeIds()) {
    states[id] = pathNodeSet.has(id) ? ['pathGlow'] : ['dimmed'];
  }

  // 为所有边分配状态，并收集路径边 ID
  const pathEdgeIds: string[] = [];
  for (const ed of graph.getEdgeData()) {
    const src = String(ed.source);
    const tgt = String(ed.target);
    const eid = String(ed.id);
    const onPath = pathEdgeKeys.has(`${src}->${tgt}`) || pathEdgeKeys.has(`${tgt}->${src}`);
    states[eid] = onPath ? ['pathGlow'] : ['dimmed'];
    if (onPath) pathEdgeIds.push(eid);
  }

  // 必须在 setElementState 之前禁用 hover-activate，
  // 否则 pointer-leave 事件会立即清除刚设置的状态
  renderer.setHoverActivateEnabled(false);

  try {
    await graph.setElementState(states);
  } catch (err) {
    console.warn('[path-highlight] setElementState failed:', err);
    renderer.setHoverActivateEnabled(true);
    return;
  }

  // 启动脉冲动画
  if (pathEdgeIds.length > 0) {
    _pathEdgeIds = pathEdgeIds;
    _activeRenderer = renderer;
    _pulsePhase = 0;
    _pulseTimer = setInterval(pulseStep, PULSE_INTERVAL);
  }
}

/**
 * 脉冲动画单步：在 pathGlow 和 pathGlowDim 之间交替切换路径边状态。
 * 使用 setElementState 的批量 API，仅更新路径边（不影响节点和非路径边的状态）。
 */
async function pulseStep(): Promise<void> {
  const graph = _activeRenderer?.getGraph();
  if (!graph || _pathEdgeIds.length === 0) {
    stopPulse();
    return;
  }

  _pulsePhase = 1 - _pulsePhase;
  const stateName = _pulsePhase === 0 ? 'pathGlow' : 'pathGlowDim';

  const edgeStates: Record<string, string[]> = {};
  for (const eid of _pathEdgeIds) {
    edgeStates[eid] = [stateName];
  }

  try {
    await graph.setElementState(edgeStates);
  } catch {
    stopPulse();
  }
}

/** 停止脉冲定时器 */
function stopPulse(): void {
  if (_pulseTimer !== null) {
    clearInterval(_pulseTimer);
    _pulseTimer = null;
  }
  _pathEdgeIds = [];
  _activeRenderer = null;
  _pulsePhase = 0;
}

/**
 * 清除所有高亮状态并停止动画。
 * 将所有节点和边的状态重置为空数组，然后重新启用 hover-activate。
 */
export async function clearHighlight(renderer: GraphRenderer): Promise<void> {
  stopPulse();

  const graph = renderer.getGraph();
  if (!graph) return;

  const states: Record<string, string[]> = {};
  for (const n of graph.getNodeData()) states[String(n.id)] = [];
  for (const e of graph.getEdgeData()) states[String(e.id)] = [];

  try {
    await graph.setElementState(states);
  } catch (err) {
    console.warn('[path-highlight] clearHighlight failed:', err);
  }

  renderer.setHoverActivateEnabled(true);
}
