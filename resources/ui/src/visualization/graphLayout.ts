/**
 * 图布局：使用 dagre 做双向树状层次布局（LR 方向）。
 * - 以无入边（或入边最少）的节点为根，前驱层在左、后继层在右。
 * - 同层节点同一 x 坐标、y 等距并整体居中对齐；根节点居中。
 * - 节点、边与边、边与节点不重叠：层间距 RANK_SEP ≥ 节点宽度 + 余量、同层 NODE_SEP ≥ 节点高度 + 余量。
 * - 节点 minWidth 200、maxWidth 360，布局占位高 60。
 */

import type { Node, Edge } from 'reactflow';
import { Position } from 'reactflow';
import type { GraphType } from '../shared/types';
import dagre from 'dagre';

const NODE_WIDTH = 280;   // 布局占位宽，取 minWidth(200)~maxWidth(360) 中间
const NODE_HEIGHT = 60;   // 布局占位高

const RANK_SEP = 120;     // 层间距（水平，LR 方向）
const NODE_SEP = 40;      // 同层节点间距（垂直）

/**
 * 使用 dagre 计算双向树状布局：LR 方向，节点间距避免重叠。
 * 返回带 position、sourcePosition、targetPosition 的节点数组。
 */
export function getLayoutedElements<T = Record<string, unknown>>(
  nodes: Node<T>[],
  edges: Edge[],
  _graphType: GraphType = 'call_graph'
): Node<T>[] {
  if (nodes.length === 0) return nodes;

  try {
    const g = new dagre.graphlib.Graph();
    g.setGraph({
      rankdir: 'LR',           // 左右方向：前驱（无入边）在左，后继在右
      nodesep: NODE_SEP,       // 同层节点（垂直方向）间距
      ranksep: RANK_SEP,       // 层间距（水平方向）
      marginx: 20,
      marginy: 20,
    });
    g.setDefaultEdgeLabel(() => ({}));

    nodes.forEach((node) => {
      g.setNode(node.id, { width: NODE_WIDTH, height: NODE_HEIGHT });
    });
    edges.forEach((edge) => {
      // 防止 dagre 抛出 edge not found 错误：只添加两端节点均存在的边
      if (g.hasNode(edge.source) && g.hasNode(edge.target)) {
        g.setEdge(edge.source, edge.target);
      }
    });

    dagre.layout(g);

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
