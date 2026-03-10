/**
 * Dagre 布局：有向图分层，避免节点重叠，支持每节点宽高。
 * 使用 @dagrejs/dagre，坐标从中心锚点转换为 React Flow 左上角锚点。
 */

import dagre from '@dagrejs/dagre';
import type { Node, Edge } from 'reactflow';

const DEFAULT_NODE_WIDTH = 220;
const DEFAULT_NODE_HEIGHT = 56;
const RANK_SEP = 80;
const NODE_SEP = 40;

export type DagreDirection = 'LR' | 'TB' | 'RL' | 'BT';

/**
 * 使用 dagre 计算节点位置，避免重叠；支持每节点 width/height。
 */
export function getLayoutedElementsDagre<T = Record<string, unknown>>(
  nodes: Node<T>[],
  edges: Edge[],
  direction: DagreDirection = 'LR'
): Node<T>[] {
  if (nodes.length === 0) return nodes;

  const g = new dagre.graphlib.Graph().setDefaultEdgeLabel(() => ({}));
  g.setGraph({ rankdir: direction, ranksep: RANK_SEP, nodesep: NODE_SEP });

  nodes.forEach((node) => {
    const w = (node.width as number) ?? DEFAULT_NODE_WIDTH;
    const h = (node.height as number) ?? DEFAULT_NODE_HEIGHT;
    g.setNode(node.id, { width: w, height: h });
  });

  edges.forEach((edge) => {
    g.setEdge(edge.source, edge.target);
  });

  dagre.layout(g);

  const isHorizontal = direction === 'LR' || direction === 'RL';
  return nodes.map((node) => {
    const dnode = g.node(node.id);
    const w = (node.width as number) ?? DEFAULT_NODE_WIDTH;
    const h = (node.height as number) ?? DEFAULT_NODE_HEIGHT;
    return {
      ...node,
      position: {
        x: dnode.x - w / 2,
        y: dnode.y - h / 2,
      },
      targetPosition: isHorizontal ? 'left' : 'top',
      sourcePosition: isHorizontal ? 'right' : 'bottom',
    };
  });
}
