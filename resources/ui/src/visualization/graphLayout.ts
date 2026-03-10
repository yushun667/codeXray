/**
 * 图布局：使用 dagre 做层次布局，自动避免节点重叠；
 * 节点宽 200、占位高 80，与 GraphNode 最大宽度一致。
 */

import type { Node, Edge } from 'reactflow';
import { Position } from 'reactflow';
import type { GraphType } from '../shared/types';
import dagre from 'dagre';

const NODE_WIDTH = 200;
const NODE_HEIGHT = 80;

/**
 * 使用 dagre 计算布局：LR 方向，节点间距避免重叠
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
      rankdir: 'LR',
      nodesep: 40,
      ranksep: 60,
      marginx: 20,
      marginy: 20,
    });

    nodes.forEach((node) => {
      g.setNode(node.id, { width: NODE_WIDTH, height: NODE_HEIGHT });
    });
    edges.forEach((edge) => {
      g.setEdge(edge.source, edge.target);
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
        sourcePosition: Position.Right,
        targetPosition: Position.Left,
      };
    });
  } catch {
    return getFallbackLayout(nodes, edges);
  }
}

/** 无 dagre 时的简单网格落位，避免重叠 */
function getFallbackLayout<T>(nodes: Node<T>[], _edges: Edge[]): Node<T>[] {
  const COLS = 5;
  const ROW_H = 100;
  const COL_W = 260;
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
