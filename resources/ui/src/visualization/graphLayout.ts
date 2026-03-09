/**
 * 使用 @dagrejs/dagre 计算节点位置，供 React Flow 使用
 */

import dagre from '@dagrejs/dagre';
import type { Node, Edge } from 'reactflow';
import type { GraphType } from '../shared/types';

const NODE_WIDTH = 180;
const NODE_HEIGHT = 36;
const RANK_SEP = 60;
const NODE_SEP = 40;

function getLayoutOptions(_graphType: GraphType): { rankdir: string; ranksep: number; nodesep: number; marginx: number; marginy: number } {
  return {
    rankdir: 'TB',
    ranksep: RANK_SEP,
    nodesep: NODE_SEP,
    marginx: 20,
    marginy: 20,
  };
}

/**
 * 将 nodes + edges 经 dagre 布局后返回带 position 的 nodes
 */
export function getLayoutedElements<T = Record<string, unknown>>(
  nodes: Node<T>[],
  edges: Edge[],
  graphType: GraphType = 'call_graph'
): Node<T>[] {
  const g = new dagre.graphlib.Graph({ compound: true });
  g.setGraph(getLayoutOptions(graphType));
  g.setDefaultEdgeLabel(() => ({}));

  nodes.forEach((node) => {
    g.setNode(node.id, { width: NODE_WIDTH, height: NODE_HEIGHT });
  });
  edges.forEach((edge) => {
    g.setEdge(edge.source, edge.target);
  });

  dagre.layout(g);

  return nodes.map((node) => {
    const pos = g.node(node.id) as { x?: number; y?: number } | undefined;
    return {
      ...node,
      position:
        pos != null && typeof pos.x === 'number' && typeof pos.y === 'number'
          ? { x: pos.x - NODE_WIDTH / 2, y: pos.y - NODE_HEIGHT / 2 }
          : node.position,
    };
  });
}
