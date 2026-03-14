/**
 * 图布局：使用 dagre 做双向树状层次布局（LR 方向）。
 *
 * 调用图布局策略（rootNodeIds 非空时启用）：
 * - 查询入口节点（root）居中放置
 * - 调用者（callers）在左侧，被调用者（callees）在右侧
 * - 通过 BFS 计算每个节点到 root 的有向距离，用 dagre rank 约束强制分层
 * - 按列对齐：左侧列右对齐、右侧列左对齐、中间列居中对齐
 *
 * 通用布局（rootNodeIds 为空或非调用图类型）：
 * - 标准 dagre LR 布局，无入边节点在左，后继在右
 *
 * 拖拽碰撞检测：
 * - 导出 NODE_WIDTH / NODE_HEIGHT 常量和 pushOverlapping() 函数
 * - GraphCore 在 onNodeDrag 中调用 pushOverlapping() 实时推开被覆盖的节点
 *
 * 节点尺寸：minWidth 200、maxWidth 360，布局占位宽 280、高 60
 */

import type { Node, Edge } from 'reactflow';
import { Position } from 'reactflow';
import type { GraphType } from '../shared/types';
import dagre from 'dagre';

/** 布局占位宽，取 minWidth(200)~maxWidth(360) 中间 */
export const NODE_WIDTH = 280;
/** 布局占位高 */
export const NODE_HEIGHT = 60;
/** 层间距（水平方向，LR 布局） */
const RANK_SEP = 120;
/** 同层节点间距（垂直方向） */
const NODE_SEP = 40;
/** 碰撞推开最小间距 */
const COLLISION_PAD = 20;

/**
 * BFS 计算从 root 节点集合出发，每个节点的有向层级：
 * - 正向 BFS（沿 source→target 边）：root 的 callees 层级 +1, +2, ...
 * - 反向 BFS（沿 target→source 边）：root 的 callers 层级 -1, -2, ...
 * - root 自身层级为 0
 *
 * @param nodeIds 所有节点 ID 集合
 * @param edges 边列表
 * @param rootIds 根节点 ID 集合
 * @returns 节点 ID → 层级（负=调用者侧，0=根，正=被调用者侧）
 */
function computeDirectedRanks(
  nodeIds: Set<string>,
  edges: Edge[],
  rootIds: Set<string>
): Map<string, number> {
  const ranks = new Map<string, number>();

  // 构建正向和反向邻接表
  const forward = new Map<string, string[]>();  // source → [targets]（callees）
  const backward = new Map<string, string[]>(); // target → [sources]（callers）
  for (const id of nodeIds) {
    forward.set(id, []);
    backward.set(id, []);
  }
  for (const e of edges) {
    if (nodeIds.has(e.source) && nodeIds.has(e.target)) {
      forward.get(e.source)!.push(e.target);
      backward.get(e.target)!.push(e.source);
    }
  }

  // 初始化根节点层级为 0
  for (const rid of rootIds) {
    if (nodeIds.has(rid)) {
      ranks.set(rid, 0);
    }
  }

  // 正向 BFS：root → callees（层级 +1, +2, ...）
  const forwardQueue: string[] = [];
  for (const rid of rootIds) {
    if (nodeIds.has(rid)) forwardQueue.push(rid);
  }
  while (forwardQueue.length > 0) {
    const cur = forwardQueue.shift()!;
    const curRank = ranks.get(cur)!;
    for (const next of forward.get(cur) ?? []) {
      if (!ranks.has(next)) {
        ranks.set(next, curRank + 1);
        forwardQueue.push(next);
      }
    }
  }

  // 反向 BFS：root → callers（层级 -1, -2, ...）
  const backwardQueue: string[] = [];
  for (const rid of rootIds) {
    if (nodeIds.has(rid)) backwardQueue.push(rid);
  }
  while (backwardQueue.length > 0) {
    const cur = backwardQueue.shift()!;
    const curRank = ranks.get(cur)!;
    for (const prev of backward.get(cur) ?? []) {
      if (!ranks.has(prev)) {
        ranks.set(prev, curRank - 1);
        backwardQueue.push(prev);
      }
    }
  }

  // 对未连通节点（BFS 未到达），分配到最右列（callees 最远层 +1）
  let maxRank = 0;
  for (const r of ranks.values()) {
    if (r > maxRank) maxRank = r;
  }
  for (const id of nodeIds) {
    if (!ranks.has(id)) {
      ranks.set(id, maxRank + 1);
    }
  }

  return ranks;
}

/**
 * 拖拽碰撞检测：检查被拖拽节点与其他节点是否重叠，推开被覆盖的节点。
 * 由 GraphCore 的 onNodeDrag 回调中调用。
 *
 * @param draggedId 正在拖拽的节点 ID
 * @param allNodes 所有节点（含拖拽中节点的最新位置）
 * @returns 需要更新位置的节点列表（不含被拖拽的节点自身），null 表示无冲突
 */
export function pushOverlapping<T>(
  draggedId: string,
  allNodes: Node<T>[]
): Node<T>[] | null {
  const dragged = allNodes.find((n) => n.id === draggedId);
  if (!dragged) return null;

  // 被拖拽节点的包围盒（position 是左上角）
  const dLeft = dragged.position.x;
  const dTop = dragged.position.y;
  const dRight = dLeft + NODE_WIDTH;
  const dBottom = dTop + NODE_HEIGHT;

  let changed = false;
  const result = allNodes.map((node) => {
    if (node.id === draggedId) return node;

    const nLeft = node.position.x;
    const nTop = node.position.y;
    const nRight = nLeft + NODE_WIDTH;
    const nBottom = nTop + NODE_HEIGHT;

    // 检查是否与被拖拽节点重叠（含间距）
    const overlapX = !(nRight + COLLISION_PAD <= dLeft || nLeft - COLLISION_PAD >= dRight);
    const overlapY = !(nBottom + COLLISION_PAD <= dTop || nTop - COLLISION_PAD >= dBottom);

    if (!overlapX || !overlapY) return node;

    // 有重叠 → 沿最小穿透方向推开
    changed = true;

    // 四个方向的穿透深度
    const pushRight = dRight + COLLISION_PAD - nLeft;   // 推向右
    const pushLeft = nRight + COLLISION_PAD - dLeft;    // 推向左
    const pushDown = dBottom + COLLISION_PAD - nTop;    // 推向下
    const pushUp = nBottom + COLLISION_PAD - dTop;      // 推向上

    // 选择最小穿透方向
    const minPush = Math.min(pushRight, pushLeft, pushDown, pushUp);

    let newX = node.position.x;
    let newY = node.position.y;

    if (minPush === pushRight) {
      newX = dRight + COLLISION_PAD;
    } else if (minPush === pushLeft) {
      newX = dLeft - NODE_WIDTH - COLLISION_PAD;
    } else if (minPush === pushDown) {
      newY = dBottom + COLLISION_PAD;
    } else {
      newY = dTop - NODE_HEIGHT - COLLISION_PAD;
    }

    return {
      ...node,
      position: { x: newX, y: newY },
    };
  });

  return changed ? result : null;
}

/**
 * 使用 dagre 计算双向树状布局：LR 方向，节点间距避免重叠。
 *
 * 当提供 rootNodeIds 时启用"根居中"模式：
 * - 通过 BFS 计算有向层级（callers 负层级在左，callees 正层级在右）
 * - 将层级归一化后作为 dagre 的 rank 约束
 *
 * dagre 自身的 nodesep/ranksep 已保证节点不重叠，无需额外碰撞检测。
 * 拖拽时的碰撞检测由 pushOverlapping() 在 GraphCore 中实时处理。
 *
 * @param nodes React Flow 节点数组
 * @param edges React Flow 边数组
 * @param _graphType 图类型
 * @param rootNodeIds 查询根节点 ID 集合（可选），用于"根居中"布局
 * @returns 带 position、sourcePosition、targetPosition 的节点数组
 */
export function getLayoutedElements<T = Record<string, unknown>>(
  nodes: Node<T>[],
  edges: Edge[],
  _graphType: GraphType = 'call_graph',
  rootNodeIds?: Set<string>
): Node<T>[] {
  if (nodes.length === 0) return nodes;

  try {
    const g = new dagre.graphlib.Graph();
    g.setGraph({
      rankdir: 'LR',           // 左右方向：调用者在左，被调用者在右
      nodesep: NODE_SEP,       // 同层节点（垂直方向）间距
      ranksep: RANK_SEP,       // 层间距（水平方向）
      marginx: 20,
      marginy: 20,
    });
    g.setDefaultEdgeLabel(() => ({}));

    // 计算有向层级（仅在有根节点时）
    const nodeIdSet = new Set(nodes.map((n) => n.id));
    const hasRoots = rootNodeIds && rootNodeIds.size > 0;
    let directedRanks: Map<string, number> | null = null;
    let rankOffset = 0;

    if (hasRoots) {
      directedRanks = computeDirectedRanks(nodeIdSet, edges, rootNodeIds);
      // dagre rank 从 0 开始，将负层级偏移为正数
      let minRank = 0;
      for (const r of directedRanks.values()) {
        if (r < minRank) minRank = r;
      }
      rankOffset = -minRank; // 偏移量：使最小层级变为 0
    }

    nodes.forEach((node) => {
      const nodeData: { width: number; height: number; rank?: number } = {
        width: NODE_WIDTH,
        height: NODE_HEIGHT,
      };
      // 设置 dagre rank 约束以强制分层
      if (directedRanks) {
        const rank = directedRanks.get(node.id);
        if (rank != null) {
          nodeData.rank = rank + rankOffset;
        }
      }
      g.setNode(node.id, nodeData);
    });

    edges.forEach((edge) => {
      // 防止 dagre 抛出 edge not found 错误：只添加两端节点均存在的边
      if (g.hasNode(edge.source) && g.hasNode(edge.target)) {
        g.setEdge(edge.source, edge.target);
      }
    });

    dagre.layout(g);

    // dagre 保证同 rank 节点 x 相同、垂直间距 ≥ nodesep，无需额外碰撞检测
    return nodes.map((node) => {
      const n = g.node(node.id);
      if (n == null) return node;
      return {
        ...node,
        position: {
          x: n.x - NODE_WIDTH / 2,
          y: n.y - NODE_HEIGHT / 2,
        },
        sourcePosition: Position.Right,  // 后继在右侧
        targetPosition: Position.Left,   // 前驱从左侧进入
      };
    });
  } catch {
    return getFallbackLayout(nodes);
  }
}

/** dagre 失败时的网格后备布局，避免重叠 */
function getFallbackLayout<T>(nodes: Node<T>[]): Node<T>[] {
  const COLS = 5;
  const ROW_H = NODE_HEIGHT + NODE_SEP;
  const COL_W = NODE_WIDTH + RANK_SEP;
  return nodes.map((node, i) => {
    const col = i % COLS;
    const row = Math.floor(i / COLS);
    return {
      ...node,
      position: { x: col * COL_W, y: row * ROW_H },
      sourcePosition: Position.Right,
      targetPosition: Position.Left,
    };
  });
}
