/**
 * G6 自定义布局：双向树状层次布局（LR 方向）
 *
 * - BFS 计算有向层级：caller 负层级在左，root 为 0，callee 正层级在右
 * - 内部使用 @dagrejs/dagre 注入 rank 约束实现根居中分层
 * - 注册为 G6 自定义布局 'bidirectional-dagre'
 *
 * 使用方式：在图初始化前调用 registerBidirectionalDagre()，
 * 然后在 GraphOptions.layout 中使用 { type: 'bidirectional-dagre', rootIds, ... }
 */

import {
  register,
  ExtensionCategory,
  BaseLayout,
  type GraphData as G6GraphData,
} from '@antv/g6';
import dagre from '@dagrejs/dagre';
import { NODE_WIDTH, NODE_HEIGHT, NODE_SEP, RANK_SEP } from './g6Config';

/** 自定义布局配置项 */
export interface BidirectionalDagreOptions {
  /** 查询根节点 ID 集合 */
  rootIds?: Set<string>;
  /** 同层节点间距（垂直），默认 NODE_SEP */
  nodesep?: number;
  /** 层间距（水平），默认 RANK_SEP */
  ranksep?: number;
}

/**
 * BFS 计算从 root 节点集合出发，每个节点的有向层级：
 * - 正向 BFS（沿 source→target 边）：root 的 callees 层级 +1, +2, ...
 * - 反向 BFS（沿 target→source 边）：root 的 callers 层级 -1, -2, ...
 * - root 自身层级为 0
 *
 * @param nodeIds  - 所有节点 ID 集合
 * @param edges    - 边列表（含 source/target 字段）
 * @param rootIds  - 根节点 ID 集合
 * @returns 节点 ID → 层级（负=调用者侧，0=根，正=被调用者侧）
 */
export function computeDirectedRanks(
  nodeIds: Set<string>,
  edges: { source: string; target: string }[],
  rootIds: Set<string>,
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

  // 对未连通节点（BFS 未到达），分配到最右列
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
 * G6 自定义布局类：双向 dagre + BFS rank 约束
 *
 * 在 dagre 基础上注入 BFS 计算的有向层级，
 * 使根节点居中，调用者在左侧、被调用者在右侧。
 */
class BidirectionalDagreLayout extends BaseLayout<BidirectionalDagreOptions> {
  id = 'bidirectional-dagre';

  /**
   * 执行布局计算
   *
   * @param model   - 包含 nodes 和 edges 的图数据
   * @param options - 布局配置项（rootIds、nodesep、ranksep）
   * @returns 带有计算后 style.x/y 的图数据
   */
  async execute(
    model: G6GraphData,
    options?: BidirectionalDagreOptions,
  ): Promise<G6GraphData> {
    const { nodes = [], edges = [] } = model;
    if (nodes.length === 0) return model;

    const opts = { ...this.options, ...options };
    const rootIds = opts.rootIds ?? new Set<string>();
    const nodesep = opts.nodesep ?? NODE_SEP;
    const ranksep = opts.ranksep ?? RANK_SEP;

    try {
      const g = new dagre.graphlib.Graph();
      g.setGraph({
        rankdir: 'LR',
        nodesep,
        ranksep,
        marginx: 20,
        marginy: 20,
        // 禁用 dagre 内置排名算法，使用我们通过 BFS 计算的精确 rank 约束。
        // dagre 默认的 network-simplex 排名器会无条件覆盖节点 rank 值，
        // 导致调用者/被调用者分层混乱（节点与根节点出现在同一列）。
        // 设置 ranker='none' 后 dagre 只负责同层内节点排序和坐标计算。
        ranker: 'none',
      });
      g.setDefaultEdgeLabel(() => ({}));

      // BFS 计算有向层级
      const nodeIdSet = new Set(nodes.map((n) => String(n.id)));
      const edgeList = edges.map((e) => ({
        source: String(e.source),
        target: String(e.target),
      }));

      let directedRanks: Map<string, number> | null = null;
      let rankOffset = 0;

      if (rootIds.size > 0) {
        directedRanks = computeDirectedRanks(nodeIdSet, edgeList, rootIds);
        // dagre rank 必须 >= 0，计算偏移量
        let minRank = 0;
        for (const r of directedRanks.values()) {
          if (r < minRank) minRank = r;
        }
        rankOffset = -minRank;
      }

      // 添加节点到 dagre（含 rank 约束）
      for (const node of nodes) {
        const nid = String(node.id);
        const nodeObj: { width: number; height: number; rank?: number } = {
          width: NODE_WIDTH,
          height: NODE_HEIGHT,
        };
        if (directedRanks) {
          const rank = directedRanks.get(nid);
          if (rank != null) {
            nodeObj.rank = rank + rankOffset;
          }
        }
        g.setNode(nid, nodeObj);
      }

      // 添加边到 dagre
      for (const edge of edges) {
        const src = String(edge.source);
        const tgt = String(edge.target);
        if (g.hasNode(src) && g.hasNode(tgt)) {
          g.setEdge(src, tgt);
        }
      }

      dagre.layout(g);

      // 读取布局结果，写入节点 style.x/y
      return {
        nodes: nodes.map((node) => {
          const nid = String(node.id);
          const pos = g.node(nid);
          if (!pos) return node;
          return {
            ...node,
            style: {
              ...(node.style ?? {}),
              x: pos.x,
              y: pos.y,
            },
          };
        }),
        edges,
      };
    } catch {
      // dagre 布局失败时使用网格后备布局
      return this.fallbackGridLayout(model);
    }
  }

  /**
   * dagre 布局失败时的网格后备布局
   *
   * @param model - 原始图数据
   * @returns 带网格位置的图数据
   */
  private fallbackGridLayout(model: G6GraphData): G6GraphData {
    const { nodes = [], edges = [] } = model;
    const COLS = 5;
    const ROW_H = NODE_HEIGHT + NODE_SEP;
    const COL_W = NODE_WIDTH + RANK_SEP;
    return {
      nodes: nodes.map((node, i) => ({
        ...node,
        style: {
          ...(node.style ?? {}),
          x: (i % COLS) * COL_W + NODE_WIDTH / 2,
          y: Math.floor(i / COLS) * ROW_H + NODE_HEIGHT / 2,
        },
      })),
      edges,
    };
  }
}

/** 标记是否已注册过，避免重复注册 */
let registered = false;

/**
 * 注册自定义布局到 G6。需在创建 Graph 实例之前调用。
 * 多次调用安全（内部有防重复注册）。
 */
export function registerBidirectionalDagre(): void {
  if (registered) return;
  register(
    ExtensionCategory.LAYOUT,
    'bidirectional-dagre',
    BidirectionalDagreLayout as any,
  );
  registered = true;
}
