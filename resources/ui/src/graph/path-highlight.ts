import type { GraphRenderer } from './renderer';

/**
 * BFS 查找从 fromId 到 toId 的最短路径。
 * 使用邻接表双向搜索，确保无论边方向如何均能找到连通路径。
 * @param fromId 起点（字符串化后的 ID）
 * @param toId 终点（字符串化后的 ID）
 * @param edgePairs [source, target] 数组（均为字符串化 ID）
 */
function bfsPath(
  fromId: string,
  toId: string,
  edgePairs: Array<[string, string]>,
): string[] | null {
  if (fromId === toId) return [fromId];

  const forward = new Map<string, string[]>();
  const backward = new Map<string, string[]>();
  for (const [src, tgt] of edgePairs) {
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
let _pathEdgeIds: Array<string | number> = [];
let _activeRenderer: GraphRenderer | null = null;

const PULSE_INTERVAL = 600;

/**
 * 高亮从根节点到目标节点的路径。
 *
 * 关键设计：所有 ID 均直接从 graph.getNodeData()/getEdgeData() 获取，
 * 避免 renderer 内部 ID 与 G6 内部元素注册表 ID 不一致的问题。
 */
export async function highlightPath(renderer: GraphRenderer, targetId: string): Promise<void> {
  const graph = renderer.getGraph();
  if (!graph) {
    console.warn('[path-highlight] no graph instance');
    return;
  }

  console.log('[path-highlight] highlightPath called, target:', targetId);

  // 先清除已有高亮
  await clearHighlight(renderer);

  const rootId = renderer.getRootId();

  // 直接从 G6 获取边数据，构建 BFS 用的边对
  const g6Edges = graph.getEdgeData();
  const edgePairs: Array<[string, string]> = g6Edges.map((e) => [
    String(e.source),
    String(e.target),
  ]);

  console.log('[path-highlight] rootId:', rootId, 'edges:', edgePairs.length);

  const path = bfsPath(String(rootId), String(targetId), edgePairs);
  if (!path || path.length === 0) {
    console.warn('[path-highlight] no path found from', rootId, 'to', targetId);
    return;
  }
  console.log('[path-highlight] path found:', path);

  // 构建路径节点集合（字符串化以确保匹配）
  const pathNodeSet = new Set(path);

  // 构建路径边 key 集合（双向匹配）
  const pathEdgeKeys = new Set<string>();
  for (let i = 0; i < path.length - 1; i++) {
    pathEdgeKeys.add(`${path[i]}->${path[i + 1]}`);
    pathEdgeKeys.add(`${path[i + 1]}->${path[i]}`);
  }

  // 使用 G6 的 getNodeData 获取实际节点 ID（避免类型不匹配）
  const g6Nodes = graph.getNodeData();
  const nodeStates: Record<string, string[]> = {};
  for (const nd of g6Nodes) {
    const id = nd.id; // 保持 G6 内部的原始 ID 类型
    const strId = String(id);
    (nodeStates as any)[id] = pathNodeSet.has(strId) ? ['pathGlow'] : ['dimmed'];
  }

  // 使用 G6 的 getEdgeData 获取实际边 ID
  const edgeStates: Record<string, string[]> = {};
  const pathEdgeIdList: Array<string | number> = [];
  for (const ed of g6Edges) {
    const eid = ed.id;
    const src = String(ed.source);
    const tgt = String(ed.target);
    const onPath = pathEdgeKeys.has(`${src}->${tgt}`) || pathEdgeKeys.has(`${tgt}->${src}`);
    (edgeStates as any)[eid!] = onPath ? ['pathGlow'] : ['dimmed'];
    if (onPath) pathEdgeIdList.push(eid!);
  }

  console.log('[path-highlight] setting states: pathNodes:', path.length,
    'pathEdges:', pathEdgeIdList.length,
    'totalNodes:', g6Nodes.length,
    'totalEdges:', g6Edges.length);

  try {
    await graph.setElementState(nodeStates);
    console.log('[path-highlight] node states set');
  } catch (err) {
    console.warn('[path-highlight] setElementState (nodes) failed:', err);
    return;
  }

  try {
    await graph.setElementState(edgeStates);
    console.log('[path-highlight] edge states set');
  } catch (err) {
    console.warn('[path-highlight] setElementState (edges) failed:', err);
    return;
  }

  // 启动脉冲动画
  if (pathEdgeIdList.length > 0) {
    _pathEdgeIds = pathEdgeIdList;
    _activeRenderer = renderer;
    _pulsePhase = 0;
    _pulseTimer = setInterval(pulseStep, PULSE_INTERVAL);
    console.log('[path-highlight] pulse animation started');
  }
}

/**
 * 脉冲动画单步：在 pathGlow 和 pathGlowDim 之间交替切换路径边状态。
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
    (edgeStates as any)[eid] = [stateName];
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
 * 使用 graph.getNodeData()/getEdgeData() 获取 ID，确保与 G6 内部一致。
 */
export async function clearHighlight(renderer: GraphRenderer): Promise<void> {
  stopPulse();

  const graph = renderer.getGraph();
  if (!graph) return;

  const nodeStates: Record<string, string[]> = {};
  for (const n of graph.getNodeData()) {
    nodeStates[String(n.id)] = [];
  }

  const edgeStates: Record<string, string[]> = {};
  for (const e of graph.getEdgeData()) {
    if (e.id != null) edgeStates[String(e.id)] = [];
  }

  try {
    await graph.setElementState(nodeStates);
  } catch (err) {
    console.warn('[path-highlight] clearHighlight (nodes) failed:', err);
  }

  try {
    await graph.setElementState(edgeStates);
  } catch (err) {
    console.warn('[path-highlight] clearHighlight (edges) failed:', err);
  }
}
